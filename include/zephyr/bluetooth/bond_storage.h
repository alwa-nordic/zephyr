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

/* unchanged from keys.h */
struct bt_bond_ltk {
	uint8_t rand[8];
	uint8_t ediv[2];
	uint8_t val[16];
};

/* changed from keys.h */
struct bt_bond_irk {
	uint8_t val[16];
	/* rpa cache removed */
};

/* unchanged from keys.h */
struct bt_bond_csrk {
	uint8_t val[16];
	uint32_t cnt;
};

/**
 * @brief Bluetooth bond storage structure
 *
 * Contains all bonding information for a paired device, including encryption keys,
 * identity information, and signing keys.
 */
struct bt_bond {
	uint8_t local_identity;
	bt_addr_le_t peer_identity_address;

	/* replaces "flags" field from keys.h */
	bool authenticated: 1;
	bool debug_key: 1;
	bool sc: 1;
	bool oob: 1;

	/* replaces "keys" field from keys.h */
	bool central_ltk_present: 1;
	bool peripheral_ltk_present: 1;
	bool irk_present: 1;
	bool local_csrk_present: 1;
	bool remote_csrk_present: 1;

	uint8_t encryption_key_size;

	struct bt_bond_ltk central_ltk;
	struct bt_bond_irk peripheral_irk;

	struct bt_bond_csrk local_csrk;
	struct bt_bond_csrk peer_csrk;
};

/* New struct. Corresponds to a "Device Identity" in the spec. */
struct bt_bond_peer_device_identity {
	uint8_t local_identity_id;
	bt_addr_le_t peer_identity_address;
	struct bt_bond_irk peer_irk;
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

	/** @brief Store a bond persistently. */
	int (*store_bond)(const struct bt_bond *bond);

	/** @brief Delete a bond matching the provided identities. */
	int (*delete_bond)(bt_addr_le_t local_identity_address,
			   bt_addr_le_t remote_identity_address);

	/** @brief Request peripheral LTK from bond storage.
	 */
	int (*get_le_peripheral_ltk)(bt_addr_le_t local_identity_address,
				     bt_addr_le_t remote_identity_address, const uint8_t ediv[2],
				     const uint8_t rand[8], uint8_t out_ltk[16]);

	/** @brief Get central LTK. */
	int (*get_le_central_ltk)(bt_addr_le_t local_identity_address,
				  bt_addr_le_t remote_identity_address, uint8_t out_ediv[2],
				  uint8_t out_rand[8], uint8_t out_ltk[16]);

	/** @brief Query authentication state for a connection. */
	int (*get_authentication)(bt_addr_le_t local_identity_address,
				  bt_addr_le_t remote_identity_address);
	int (*local_csrk_read)(void);
	int (*local_csrk_increment_counter)(void);

	int (*remote_csrk_read)(void);
	int (*remote_csrk_increment_counter)(void);

	/** @brief Read all remote device identity entries and notify host.
	 *
	 * Non-normative: The Host likes to put "everything" on the
	 * Resolve List. All the bonded peers have their IRK there
	 * always.
	 *
	 * Normative: Please call back into Host with the Device
	 * Identity entry for each bond that is stored.
	 */
	int (*for_each_remote_device_identity)(
		void (*callback)(bt_addr_le_t remote_identity_address));
};

#endif /* ZEPHYR_SUBSYS_BLUETOOTH_HOST_BOND_STORAGE_H_ */
