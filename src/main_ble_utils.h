#ifndef MAIN_LOKI_BLE_H
#define MAIN_LOKI_BLE_H


#define MAX_LEN_FULL_NAME 63
#define MAX_LEN_BLE_NAME 8
#define DEFAULT_NAME_PREFIX  "TREN"

 extern char ble_name[MAX_LEN_BLE_NAME+1]; // = e.g. "TREN0234" in   main_ble_utils.c
 extern char full_name[MAX_LEN_FULL_NAME+1]; // = e.g. "Keihan Otsu Line Type 700 [Sound! Euphonium] Wrapping Train 2023" in main_ble_utils.c
 extern uint16_t dcc_address; // main_loki.h

 char *getBleShortName();
 char *getBleLongName();
 int updateBleLongName(char *newName);
 int updateBleShortName(char *newName);
 int get_Eui64(char *printable);

void bt_submit_start_advertising_work();
void bt_submit_refresh_advertising_data_work();

static atomic_t ble_should_advertise = ATOMIC_INIT(1);

extern void bt_notify_speed(void);
extern void bt_register(void);
extern int bt_ready(void);

/* BLE advertising lifecycle controller.
 * Drives the "BLE off after a successful Thread attach" behavior governed by
 * CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES. Called from the OpenThread state
 * change callback and from the /ble-recovery CoAP resource.
 */
void ble_lifecycle_on_thread_attached(void);
void ble_lifecycle_on_thread_detached(void);
void ble_lifecycle_force_recovery(void);

#endif /* MAIN_LOKI_BLE_H */