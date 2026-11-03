#ifndef EVLAMP_BONDS_H_
#define EVLAMP_BONDS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

/**
 * Names of the bonded phones, kept next to the bond store.
 *
 * NimBLE stores the keys and the identity address of every bond, but not the phone's name:
 * pairing (SMP) exchanges keys only. The app reads the Bluetooth name shown in the phone's
 * settings (BluetoothAdapter.getName()) and sends it once with Command 0x0A; the lamp keeps it
 * in NVS here, keyed by the identity address. Names of phones that are no longer bonded are
 * pruned, and factory reset clears them with the bonds.
 */

typedef struct
{
    uint8_t type;       // BLE address type, as in ble_addr_t.type
    uint8_t addr[6];    // identity address, least significant byte first
} bond_id_t;

/** Stores (or replaces) the name of a phone; an empty name forgets it. */
esp_err_t bonds_set_name(const bond_id_t *id, const char *name);

/** Copies the name of a phone into out; returns its length, 0 when there is no name. */
size_t bonds_get_name(const bond_id_t *id, char *out, size_t size);

/** Drops the name of one phone. */
void bonds_forget(const bond_id_t *id);

/** Drops the names of phones that are not in the list (a bond was evicted or forgotten). */
void bonds_prune(const bond_id_t *ids, size_t count);

/** Drops every name (factory reset). */
void bonds_clear(void);

#endif /* EVLAMP_BONDS_H_ */
