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

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <stdint.h>
#include <zephyr/sys/util_macro.h>

/**
 * @brief LTK container (legacy or SC).
 */
struct bt_bond_ltk {
	uint8_t rand[8];
	uint8_t ediv[2];
	uint8_t val[16];
};

/* changed from keys.h */
/**
 * @brief IRK container.
 * QUESTION: where/how do we store RPA? Maybe we need to add something here? Where else?
 */
struct bt_bond_irk {
	uint8_t val[16];
	/* rpa cache removed */
};

/* unchanged from keys.h */
/**
 * @brief CSRK container.
 */
struct bt_bond_csrk {
	uint8_t val[16];
	uint32_t cnt; /**< Signing counter. */
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
	/**
	 * @brief Local identity index.
	 * @note Formerly, in bt_keys, this was just "id"
	 */
	uint8_t local_identity;

	/**
	 * @brief  Peer identity address.
	 * @note Formerly, in bt_keys, this was just "addr"
	 */
	bt_addr_le_t peer_identity_address;

	/**
	 * @brief Negotiated encryption key size (1..16).
	 * @note Formerly, in bt_keys, this was just "enc_size"
	 */
	uint8_t encryption_key_size;
	/**
	 * TODO: This an artifact of some AI documentation? I am confused by this comment block.
	 * Encryption weakening feature used in some countries.
	 */

	/* replaces "flags" field from keys.h */
	/**
	 * @brief Security level information.
	 */
	bool authenticated: 1; /** Not Just-works. */
	bool debug_key: 1; /**< Either local or peer used the well known debug private key during
			      diffie-hellman. QUESTION: Why are we storing this? Is it used for
			      anything currently? Is is just a 'nice to have'? */
	bool lesc: 1;      /** Low Energy Secure Connections method was used to create the bond. */
	bool oob: 1;       /** Out of Band information was used to authenticate the bond. */

	/**
	 * QUESTION: Do we need a bool or is the zero LTK invalid?
	 */
	bool central_ltk_present: 1;
	bool peripheral_ltk_present: 1;
	bool irk_present: 1;
	bool local_csrk_present: 1;
	bool remote_csrk_present: 1;
	/**
	 * @brief LTK to use when local is central.
	 *
	 * @note If this bond was made using LESC (Low Energy
	 * Secure Connection), this LTK (Long Term Key) is an
	 * exact copy of @ref peripheral_ltk.
	 */
	struct bt_bond_ltk central_ltk;

	/**
	 * @brief LTK to use when local is peripheral.
	 *
	 * @note If this bond was made using LESC (Low Energy
	 * Secure Connection), this LTK (Long Term Key) is an
	 * exact copy of @ref central_ltk.
	 */
	struct bt_bond_ltk peripheral_ltk;

	/**
	 * @brief Peer Identity Resolving Key
	 * @note IRK zero is invalid.
	 */
	struct bt_bond_irk peer_irk;

	/**
	 * @brief Local Connection Signature Resolving Key.
	 * This is a legacy signing feature, modern implementations use link encryption and
	 * Encrypted Advertising Data (EAD) instead.
	 */
	struct bt_bond_csrk local_csrk;

	/**
	 * @brief Remote Connection Signature Resolving Key.
	 * This is a legacy signing feature, modern implementations use link encryption and
	 * Encrypted Advertising Data (EAD) instead.
	 */
	struct bt_bond_csrk peer_csrk;
};

/* New struct. Corresponds to a "Device Identity" in the spec. */
struct bt_bond_peer_device_identity {
	uint8_t local_identity_id;
	bt_addr_le_t peer_identity_address;
	struct bt_bond_irk peer_irk;
};

/**
 * @brief Bond storage API table
 *
 * Contains function pointers implemented by a bond-storage backend. Callers
 * should use the `bond_storage` instance exported by the chosen backend.
 *
 * QUESTION: Make into one function pointer for optimization?
 *
 * QUESTION: Should we be giving the conn pointer or the local and remote
 * address instead? If we give the conn pointer, we can access
 * all the necessary information from it. Using addresses would
 * decouple the API from bt_conn. We also don't always have an
 * active connection, for example when we are looking for
 * conflicts.
 */
struct bond_storage_api {

	/** @brief Store a bond persistently.
	 *
	 * TODO: Determine what happens if a bond with the same
	 * bond identity already exists. Is it updated? Or is
	 * that an error?
	 */
	int (*store_bond)(const struct bt_bond *bond);

	/** @brief Delete a bond matching the provided identities. */
	int (*delete_bond)(uint8_t local_identity, bt_addr_le_t remote_identity_address);
	/** @brief Request peripheral LTK from bond storage.
	*************************************/

	/** @brief Get peripheral LTK from the bond storage.*/
	int (*get_le_peripheral_ltk)(uint8_t local_identity, bt_addr_le_t remote_identity_address,
				     const uint8_t ediv[2], const uint8_t rand[8],
				     uint8_t out_ltk[16]);

	/** @brief Get central LTK from the bond storage. */
	int (*get_le_central_ltk)(uint8_t local_identity, bt_addr_le_t remote_identity_address,
				  uint8_t out_ediv[2], uint8_t out_rand[8], uint8_t out_ltk[16]);

	/** @brief Get peer IRK from the bond storage. */
	int (*get_peer_irk)(uint8_t local_identity, bt_addr_le_t remote_identity_address,
			    uint8_t out_irk[16]);

	/** @brief Get flag states from bond_storage. */
	int (*get_is_authenticated)(uint8_t local_identity, bt_addr_le_t remote_identity_address);
	int (*get_is_lesc)(uint8_t local_identity, bt_addr_le_t remote_identity_address);
	int (*get_is_oob)(uint8_t local_identity, bt_addr_le_t remote_identity_address);
	int (*get_is_debug_key)(uint8_t local_identity, bt_addr_le_t remote_identity_address);

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
	int (*read_out_all_remote_device_identities)(struct bt_bond_peer_device_identity *outs);

	/** @brief Read local CSRK. */
	int (*local_csrk_read)(void);

	/** @brief Increment local CSRK counter. */
	int (*local_csrk_increment_counter)(void);

	/** @brief Read remote CSRK. */
	int (*remote_csrk_read)(void);

	/** @brief Increment remote CSRK counter. */
	int (*remote_csrk_increment_counter)(void);
};
#endif /* ZEPHYR_SUBSYS_BLUETOOTH_HOST_BOND_STORAGE_H_ */
