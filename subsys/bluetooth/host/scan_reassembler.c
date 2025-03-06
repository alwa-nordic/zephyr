#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/check.h>
#include <zephyr/net_buf.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/direction.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>

#include "scan_reassembler.h"
#include "hci_core.h"
#include "scan.h"

#define LOG_LEVEL CONFIG_BT_HCI_CORE_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_scan_reassembler);

NET_BUF_POOL_FIXED_DEFINE(ext_scan_pool, 1, CONFIG_BT_EXT_SCAN_BUF_SIZE, 0, NULL);

static K_MUTEX_DEFINE(bt_scan_reassembler_mutex);
static size_t bt_scan_head_remaining_report_count;
/**
 * Appending to this list using net_buf_slist_put() is thread-safe.
 *
 * Removing from this list is allowed only when holding the bt_scan_reassembler_mutex.
 */
static sys_slist_t bt_scan_pending_adv_reports;

struct fragmented_advertiser {
	/* If NULL, data should be discarded. */
	struct net_buf *buf;
	bt_addr_le_t addr;
	uint8_t sid;
};

static struct fragmented_advertiser reassembling_advertiser;

static bool fragmented_advertisers_equal(const struct fragmented_advertiser *a,
					 const bt_addr_le_t *addr, uint8_t sid)
{
	/* Two advertisers are equal if they are the same adv set from the same device */
	return a->sid == sid && bt_addr_le_eq(&a->addr, addr);
}

static bool reassembling_advertiser_can_alloc(void)
{
	return bt_addr_le_eq(&reassembling_advertiser.addr, &(bt_addr_le_t){});
}

/* Sets the address and sid of the advertiser to be reassembled. */
static void init_reassembling_advertiser(const bt_addr_le_t *addr, uint8_t sid)
{
	bt_addr_le_copy(&reassembling_advertiser.addr, addr);
	reassembling_advertiser.sid = sid;
	reassembling_advertiser.buf = net_buf_alloc(&ext_scan_pool, K_NO_WAIT);
}

static void reset_reassembling_advertiser(struct fragmented_advertiser *reassembly)
{
	if (reassembly->buf) {
		net_buf_unref(reassembly->buf);
		reassembly->buf = NULL;
	}
	reassembly->addr = (bt_addr_le_t){};
	reassembly->sid = 0;
}

void bt_scan_reassembler_reset(void)
{
	k_mutex_lock(&bt_scan_reassembler_mutex, K_FOREVER);
	reset_reassembling_advertiser(&reassembling_advertiser);
	for (struct net_buf *buf; (buf = net_buf_slist_get(&bt_scan_pending_adv_reports));) {
		net_buf_unref(buf);
	}
	bt_scan_head_remaining_report_count = 0;
	k_mutex_unlock(&bt_scan_reassembler_mutex);
}

/* Convert Extended adv report evt_type field into adv type */
static uint8_t get_adv_type(uint8_t evt_type)
{
	switch (evt_type) {
	case (BT_HCI_LE_ADV_EVT_TYPE_CONN |
	      BT_HCI_LE_ADV_EVT_TYPE_SCAN |
	      BT_HCI_LE_ADV_EVT_TYPE_LEGACY):
		return BT_GAP_ADV_TYPE_ADV_IND;

	case (BT_HCI_LE_ADV_EVT_TYPE_CONN |
	      BT_HCI_LE_ADV_EVT_TYPE_DIRECT |
	      BT_HCI_LE_ADV_EVT_TYPE_LEGACY):
		return BT_GAP_ADV_TYPE_ADV_DIRECT_IND;

	case (BT_HCI_LE_ADV_EVT_TYPE_SCAN |
	      BT_HCI_LE_ADV_EVT_TYPE_LEGACY):
		return BT_GAP_ADV_TYPE_ADV_SCAN_IND;

	case BT_HCI_LE_ADV_EVT_TYPE_LEGACY:
		return BT_GAP_ADV_TYPE_ADV_NONCONN_IND;

	case (BT_HCI_LE_ADV_EVT_TYPE_SCAN_RSP |
	      BT_HCI_LE_ADV_EVT_TYPE_CONN |
	      BT_HCI_LE_ADV_EVT_TYPE_SCAN |
	      BT_HCI_LE_ADV_EVT_TYPE_LEGACY):
	case (BT_HCI_LE_ADV_EVT_TYPE_SCAN_RSP |
	      BT_HCI_LE_ADV_EVT_TYPE_SCAN |
	      BT_HCI_LE_ADV_EVT_TYPE_LEGACY):
		/* Scan response from connectable or non-connectable advertiser.
		 */
		return BT_GAP_ADV_TYPE_SCAN_RSP;

	default:
		return BT_GAP_ADV_TYPE_EXT_ADV;
	}
}

/* Convert Extended adv report PHY to GAP PHY */
static uint8_t get_ext_adv_coding_sel_phy(uint8_t hci_phy)
{
	/* Converts from Extended adv report PHY to BT_GAP_LE_PHY_*
	 * When Advertising Coding Selection (Host Support) is enabled
	 * the controller will return the advertising coding scheme which
	 * can be S=2 or S=8 data coding.
	 */
	switch (hci_phy) {
	case BT_HCI_LE_ADV_EVT_PHY_1M:
		return BT_GAP_LE_PHY_1M;
	case BT_HCI_LE_ADV_EVT_PHY_2M:
		return BT_GAP_LE_PHY_2M;
	case BT_HCI_LE_ADV_EVT_PHY_CODED_S8:
		return BT_GAP_LE_PHY_CODED_S8;
	case BT_HCI_LE_ADV_EVT_PHY_CODED_S2:
		return BT_GAP_LE_PHY_CODED_S2;
	default:
		return 0;
	}
}

/* Convert extended adv report evt_type field to adv props */
static uint16_t get_adv_props_extended(uint16_t evt_type)
{
	/* Converts from BT_HCI_LE_ADV_EVT_TYPE_* to BT_GAP_ADV_PROP_*
	 * The first 4 bits are the same (conn, scan, direct, scan_rsp).
	 * Bit 4 must be flipped as the meaning of 1 is opposite (legacy -> extended)
	 * The rest of the bits are zeroed out.
	 */
	return (evt_type ^ BT_HCI_LE_ADV_EVT_TYPE_LEGACY) & BIT_MASK(5);
}

static void create_ext_adv_info(struct bt_hci_evt_le_ext_advertising_info const *const evt,
				struct bt_le_scan_recv_info *const scan_info)
{
	if (IS_ENABLED(CONFIG_BT_EXT_ADV_CODING_SELECTION) &&
	    BT_FEAT_LE_ADV_CODING_SEL(bt_dev.le.features)) {
		scan_info->primary_phy = get_ext_adv_coding_sel_phy(evt->prim_phy);
		scan_info->secondary_phy = get_ext_adv_coding_sel_phy(evt->sec_phy);
	} else {
		scan_info->primary_phy = bt_get_phy(evt->prim_phy);
		scan_info->secondary_phy = bt_get_phy(evt->sec_phy);
	}

	scan_info->tx_power = evt->tx_power;
	scan_info->rssi = evt->rssi;
	scan_info->sid = evt->sid;
	scan_info->interval = sys_le16_to_cpu(evt->interval);
	scan_info->adv_type = get_adv_type(sys_le16_to_cpu(evt->evt_type));
	scan_info->adv_props = get_adv_props_extended(sys_le16_to_cpu(evt->evt_type));
}

/* Subreports are processed in a critical section in the following manner:

There is a report processing "read head" that moves only in the critical section.

and will holds a reference to a
HCI ext adv report buffer.
 taking an HCI report buf from the queue

Removing from the queue is only allowed in the critical section.

 *
 *
 * This function will process subreports in a critical section until:
    - The report queue is empty. Just exit.
 *  - A complete subreport is encountered. If the callback is
 *    disabled, we simply skip it and continue. Otherwise,
 *    we take an extra reference to the report net_buf, leave the critical
 *    section, invoke the callback, then release the extra reference.
 *  - A complete report has been reassembled. If the callback is
      disabled, we reset the reassembler, discarding the
      reassembled report. Otherwise, ensure the reassembler state is "processing callback",
 *    the same critical section, is  case the buffer holding the
 *    data will now be kept alive and used to invoke the
 *    application callback, or finally the report queue is
 *    empty.
        - We reach the end of the net_buf was removed from the
          queue. This is a good time to yield by exiting. If the
          queue is not empty, we schedule the continuation of
          processing before exit.
 *
 * Any number of threads may invoke this function, but it's intended for
 * two. One thread is the normal thread to invoke this function, with
 * cb_enabled.
 *
 * When cb_enabled is true, the application callbacks are run. Otherwise
 * the report is dropped.
 *
 * blocking if cb_enabled is true
 * invokes at most one application callback if cb_enabled is true
 * non-blocking if cb_enabled is false
 * thread-safe

 This function takes care to not reorder advertising reports.
 */

typedef void bt_scan_result_cb_t(bt_addr_le_t *addr, struct bt_le_scan_recv_info *info, struct net_buf_simple *buf,
	uint16_t len);
static void bt_scan_result_process_one(bt_scan_result_cb_t *cb)
{
	struct net_buf *buf = NULL;
	struct net_buf *cb_buf_ownership = NULL;
	struct bt_hci_evt_le_ext_advertising_info *cb_evt = NULL;

	/* Start of critical section */
	k_mutex_lock(&bt_scan_reassembler_mutex, K_FOREVER);

	/* Important: Inside the critical section shall be ISR
	 * safe. Do not invoke any callbacks here. That also
	 * prohibits the use of `net_buf_unref` on arbitrary
	 * buffers. Only buffers with known isr-safe
	 * `net_buf_unref`.
	 */

	/* Give callbacks for any reports that were reassembled while callback were disabled. */
	// TODO

	/* While we have the mutex, we are borrowing buf from the list. The borrow must end before the mutex is released. */
	buf = (void *)sys_slist_peek_head(&bt_scan_pending_adv_reports);

	if (!buf) {
		/* There are no reports available from the Controller.
		 * Return without requesting more processing time.
		 */
		 goto exit;
	}

	/* Invariant: If `bt_scan_head_remaining_report_count == 0` at the
	 * start of the critical section, `buf` is a new HCI
	 * ext adv report that has not yet had its length header removed.
	 */
	if (bt_scan_head_remaining_report_count == 0) {
		if (buf->len == 0) {
			LOG_WRN("ext adv report missing length");
			goto exit;
		}

		bt_scan_head_remaining_report_count = net_buf_pull_u8(buf);

		if (!IN_RANGE(bt_scan_head_remaining_report_count, 1, 0x0a)) {
			LOG_ERR("Non-conformant Num_Reports %d", bt_scan_head_remaining_report_count);
		}
	}

	while (bt_scan_head_remaining_report_count > 0) {
		struct bt_hci_evt_le_ext_advertising_info *evt;
		uint16_t data_status;
		uint16_t evt_type;
		bool is_report_complete;
		bool more_to_come;
		bool is_new_advertiser;

		bt_scan_head_remaining_report_count--;

		if (buf->len < sizeof(*evt)) {
			/* EIO */
			LOG_ERR("Unexpected end of buffer 1");
			bt_scan_head_remaining_report_count = 0;
			goto exit;
		}

		evt = net_buf_pull_mem(buf, sizeof(*evt));
		evt_type = sys_le16_to_cpu(evt->evt_type);
		data_status = BT_HCI_LE_ADV_EVT_TYPE_DATA_STATUS(evt_type);
		is_report_complete = data_status == BT_HCI_LE_ADV_EVT_TYPE_DATA_STATUS_COMPLETE;
		more_to_come = data_status == BT_HCI_LE_ADV_EVT_TYPE_DATA_STATUS_PARTIAL;

		if (evt->length > buf->len) {
			/* EIO */
			LOG_WRN("Adv report corrupted (wants %u out of %u)", evt->length, buf->len);
			bt_scan_head_remaining_report_count = 0;
			if (reassembling_advertiser.buf) {
				net_buf_unref(reassembling_advertiser.buf);
				reassembling_advertiser.buf = NULL;
			}
			goto exit;
		}

		/* Pull the event data. It's accessible trough `evt->data`. */
		net_buf_pull(buf, evt->length);

		if (evt_type & BT_HCI_LE_ADV_EVT_TYPE_LEGACY) {
			cb_buf_ownership = net_buf_ref(buf);
			cb_evt = evt;
			break;
		}

		is_new_advertiser = !fragmented_advertisers_equal(&reassembling_advertiser,
								  &evt->addr, evt->sid);

		if (is_new_advertiser && is_report_complete) {
			cb_buf_ownership = net_buf_ref(buf);
			cb_evt = evt;
			break;
		}

		if (is_new_advertiser && !reassembling_advertiser_can_alloc()) {
			/* The Controller is interleaving fragmented advertising
			 * reports. This is legal but not supported in this
			 * Host, and no Controller known to us does it.
			 *
			 * After this error occurs, scan results provided to the
			 * application may be truncated at the begining. This is
			 * considered acceptable and must be tolerated by the
			 * application since the situation is as-if a malicious
			 * advertiser sent the truncated advertisement on
			 * purpose.
			 *
			 * Supporting this would require an unbounded amount of
			 * memory, as we have to store the address and sid of
			 * the advertising set to drop. This is a consequence of
			 * the lack of a flag in the HCI report that it contains
			 * first fragment. Unfortunately there is only a final/
			 * not-final flag.
			 *
			 * It's possible to add support for a bounded number of
			 * interleaved advertisement reports.
			 */
			LOG_ERR("Interleaved adv");
			continue;
		}

		if (data_status == BT_HCI_LE_ADV_EVT_TYPE_DATA_STATUS_INCOMPLETE) {
			/* The Controller is aborting the reassembly. We
			 * discard the partially received report and do
			 * not notify the application.
			 *
			 * See the Controller's documentation for possible
			 * reasons for aborting. Hint:
			 * CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX.
			 */
			LOG_DBG("Incomplete adv");
			reset_reassembling_advertiser(&reassembling_advertiser);
			continue;
		}

		/* Start reassembly */
		if (is_new_advertiser) {
			/* We are not reassembling reports from an advertiser and
			 * this is the first report from the new advertiser.
			 * Initialize the new advertiser.
			 */
			init_reassembling_advertiser(&evt->addr, evt->sid);
		}

		/* Append to reassembler */
		if (reassembling_advertiser.buf) {
			if (net_buf_tailroom(reassembling_advertiser.buf) > evt->length) {
				net_buf_add_mem(reassembling_advertiser.buf, buf->data,
						evt->length);
			} else {
				/* The report does not fit in the reassembly buffer
				 * Discard this and future reports from the advertiser.
				 */
				LOG_WRN("Oversize advertisement");
				net_buf_unref(reassembling_advertiser.buf);
				reassembling_advertiser.buf = NULL;
			}
		}

		/* Finish reassembly */
		if (!more_to_come) {
			if (reassembling_advertiser.buf) {
				cb_buf_ownership = reassembling_advertiser.buf;
				reassembling_advertiser.buf = NULL;
				cb_evt = evt;
			}

			reset_reassembling_advertiser(&reassembling_advertiser);

			if (cb_evt) {
				goto exit;
			}
		}

	}

exit:
	/* Before exiting the critical section, restore the invariant. */
	if (bt_scan_head_remaining_report_count == 0 && buf) {
		/* The remaining count is 0, the invariant is upheld by removing `buf` before we exit. */
		buf = net_buf_slist_get(&bt_scan_pending_adv_reports);
		net_buf_unref(buf);
		buf = NULL;
	}

	/* End of critical section */
	k_mutex_unlock(&bt_scan_reassembler_mutex);

	/* Now we invoke application callbacks */
	if (cb_evt && cb) {
		struct bt_le_scan_recv_info scan_info;
		struct net_buf_simple scan_data;

		create_ext_adv_info(cb_evt, &scan_info);
		net_buf_simple_init_with_data(&scan_data, cb_evt->data, cb_evt->length);
		cb(&cb_evt->addr, &scan_info, &scan_data, scan_data.len);
	}

	if (cb_buf_ownership) {
		net_buf_unref(cb_buf_ownership);
	}
}

void bt_scan_append_ext_adv_report(struct net_buf *buf)
{
	net_buf_slist_put(&bt_scan_pending_adv_reports, net_buf_ref(buf));
	bt_hci_core_trigger_rx_work();
}

bool bt_scan_rx_work_pending(void)
{
	return !sys_slist_is_empty(&bt_scan_pending_adv_reports);
}

void bt_scan_rx_work(void)
{
	bt_scan_result_process_one(le_adv_recv);

	if (bt_scan_rx_work_pending()) {
		bt_hci_core_trigger_rx_work();
	}
}

void bt_scan_drop_buf(void)
{
	bt_scan_result_process_one(NULL);
}
