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


extern void bt_notify_speed(void);
extern void bt_register(void);
extern int bt_ready(void);
#endif /* MAIN_LOKI_BLE_H */