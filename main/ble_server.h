#ifndef EVLAMP_BLE_SERVER_H_
#define EVLAMP_BLE_SERVER_H_

#include "common.h"

/**
 * BLE GATT server, see docs/BLE_PROTOCOL.md
 */

/**
 * Start NimBLE, register the service and start advertising.
 * The pairing window is open for BLE_PAIRING_WINDOW_S after this call.
 */
esp_err_t ble_server_init();

/**
 * Allow new phones to pair for BLE_PAIRING_WINDOW_S
 */
void ble_server_open_pairing_window();

/**
 * Forget all bonded phones
 */
void ble_server_forget_bonds();

#endif /* EVLAMP_BLE_SERVER_H_ */
