/** @file
 *  @brief Bluetooth bond storage API
 */

/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_BLUETOOTH_HOST_BOND_STORAGE_H_
#define ZEPHYR_SUBSYS_BLUETOOTH_HOST_BOND_STORAGE_H_

/**
What is “part” of a bond?

- Identity / address pair (local identity id + peer address)
- Optional: Central LTK (central/legacy or LE SC shared slot)
	- Rand & EDIV (For the peripheral to use to potentially derive the LTK from a Root Key)
- Optional: Peripheral LTK (For LESC, this is same LTK as central LTK)
	- Rand & EDIV (TODO: Do we need this? There is only one peripheral LTK, so we know which LTK
to use)
- Encryption key size (We'll keep this for now, even though we should probably just reject smaller
keys. Let's ask PMT.)
- Key flags
  - Authenticated: whether the LTK was generated from an authenticated pairing.
  - Debug: whether the LTK was generated from a debug pairing (TODO: Why are we storing this?)
  - Not broken authentication: One of the following is true. Legacy passkey authentication is not
considered secure. (TODO: clarify)
    - Whether the LTK was generated from LE Secure Connections pairing
    - Whether the LTK was generated from Out-of-Band pairing
	- SC: whether the LTK was generated from LE Secure Connections pairing
	- OOB: whether the LTK was generated from Out-of-Band pairing
- Optional: IRK: (Identity Resolving Key)
- Optional: CSRK: (local and remote signing keys) + counter

### API and other stuff?
- Persistent storage (settings subsystem interaction)
- Runtime retrieval / lazy loading / resolution
- Clearing / deletion / overwrite logic


TK (Temporary Key) is not stored in bond storage.
*/

/*** Structs ***/
/********************************************/
/********************************************/
/********************************************/

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <stdint.h>
#include <zephyr/sys/util_macro.h>

enum bt_keys_type {
	BT_KEYS_PERIPH_LTK = BIT(0),
	BT_KEYS_IRK = BIT(1),
	BT_KEYS_LTK = BIT(2),
	BT_KEYS_LOCAL_CSRK = BIT(3),
	BT_KEYS_REMOTE_CSRK = BIT(4),
	BT_KEYS_LTK_P256 = BIT(5),
};

enum {
	BT_KEYS_AUTHENTICATED = BIT(0),
	BT_KEYS_DEBUG = BIT(1),
	/* Bit 2 and 3 might accidentally exist in old stored keys */
	BT_KEYS_SC = BIT(4),
	BT_KEYS_OOB = BIT(5),
};

struct bt_ltk {
	uint8_t rand[8];
	uint8_t ediv[2];
	uint8_t val[16];
};

struct bt_irk {
	uint8_t val[16];
};

struct bt_csrk {
	uint8_t val[16];
	uint32_t cnt;
};

/**
 * @brief Bluetooth bond storage structure
 *
 * Contains all bonding information for a paired device, including encryption keys,
 * identity information, and signing keys.
 *
 * @note The @ref state field is runtime-only and not persisted to flash.
 * All other fields following @ref storage_start are stored in flash memory.
 */
struct bt_bond {
	uint8_t local_identity;

	bt_addr_le_t peer_identity_address;
	/**<
	 */

	uint8_t encryption_key_size;
	/**<
	 * Encryption weakening feature used in some countries.
	 */

	/**
	 * @brief Security level information.
	 * Bitfield containing BT_KEYS_AUTHENTICATED, BT_KEYS_DEBUG, BT_KEYS_SC, and BT_KEYS_OOB
	 * flags.
	 */
	uint8_t flags;

	/**
	 * @brief Key type bitfield indicating which of the following key fields are valid.
	 * Uses @ref bt_keys_type values.
	 */
	uint16_t keys;

	/**
	 * @brief Central Long Term Key.
	 * For LE Secure Connections (LESC), this is also used as the peripheral LTK.
	 */
	struct bt_ltk ltk;

	/** @brief Peer Identity Resolving Key */
	struct bt_irk irk;

	/**
	 * @brief Local Connection Signature Resolving Key.
	 * Legacy signing feature; modern implementations use link encryption and
	 * Encrypted Advertising Data (EAD) instead.
	struct bt_csrk local_csrk;  	// Signing stuff that nobody uses anymore. Instead we just
	encrypt the link, and use EAD for advertisements.
	 */
	struct bt_csrk local_csrk;

	/**
	 * @brief Remote Connection Signature Resolving Key.
	 * Legacy signing feature; modern implementations use link encryption instead.
	 */
	struct bt_csrk remote_csrk;
};

/*** Hereafter start function definitions ***/
/********************************************/
/********************************************/
/********************************************/

/**
 * @brief Bond storage API table
 *
 * Contains function pointers implemented by a bond-storage backend. Callers
 * should use the `bond_storage` instance exported by the chosen backend.
 *
 * Make into one function pointer for optimization?
 *
 * Should we be giving the conn pointer or the local and remote
 * address instead? If we give the conn pointer, we can access
 * all the necessary information from it. Using addresses would
 * decouple the API from bt_conn. We also don't always have an
 * active connection, for example when we are looking for
 * conflicts.
 */
struct bond_storage_api {
	/** @brief Request peripheral LTK from bond storage.
	 */
	int (*get_le_peripheral_ltk)(struct bt_conn *conn, const uint8_t ediv[2],
				     const uint8_t rand[8], uint8_t out_ltk[16]);

	/** @brief Get central LTK. */
	int (*get_le_central_ltk)(struct bt_conn *conn, uint8_t out_ediv[2], uint8_t out_rand[8],
				  uint8_t out_ltk[16]);

	/** @brief Get BR/EDR link key. */
	int (*get_bredr_link_key)(struct bt_conn *conn, uint8_t out_key[16]);

	/** @brief Query authentication state for a connection. */
	int (*get_authentication)(struct bt_conn *conn);

	/** @brief Look up potential conflicts when adding a bond. */
	int (*lookup_conflicts)(void);

	/** @brief Store a bond persistently. */
	int (*store_bond)(const struct bt_bond *bond);

	/** @brief Delete a bond matching the provided identities. */
	int (*delete_bond)(bt_addr_le_t local_identity_address,
			   bt_addr_le_t remote_identity_address);

	/** @brief Read all remote device identity entries and notify host.
	 *
	 * Non-normative: The Host likes to put "everything" on the
	 * Resolve List. All the bonded peers have their IRK there
	 * always.
	 *
	 * Normative: Please call back into Host with the Device
	 * Identity entry for each bond that is stored.
	 *
	 * TODO: What is the callback? Does the Host need to know when
	 * the "foreach" has completed? How?
	 */
	int (*read_all_remote_device_identities)(void);

	/** @brief Read local CSRK. */
	int (*local_csrk_read)(void);

	/** @brief Increment local CSRK counter. */
	int (*local_csrk_increment_counter)(void);

	/** @brief Read remote CSRK. */
	int (*remote_csrk_read)(void);

	/** @brief Increment remote CSRK counter. */
	int (*remote_csrk_increment_counter)(void);
};

/* Backend should provide this instance. Keeping it `const` encourages statically
 * defined tables in implementations.
 */
extern const struct bond_storage_api bond_storage;
/* End of function API table */

#endif /* ZEPHYR_SUBSYS_BLUETOOTH_HOST_BOND_STORAGE_H_ */
