#ifndef MAIN_LOKI_BLE_H
#define MAIN_LOKI_BLE_H


#define MAX_LEN_FULL_NAME 63
#define MAX_LEN_BLE_NAME 8
#define DEFAULT_NAME_PREFIX "LOKI";
static char ble_name[MAX_LEN_BLE_NAME+1] = "LOKI";
static char full_name[MAX_LEN_FULL_NAME+1] = "LOKI";
static uint16_t dcc_address = 3;

 char *getBleShortName();
 char *getBleLongName();
 int updateBleLongName(char *newName);
 int updateBleShortName(char *newName);
 int get_Eui64(char *printable);

extern void bt_notify_speed(void);
extern void bt_register(void);
extern int bt_ready(void);
#endif /* MAIN_LOKI_BLE_H */