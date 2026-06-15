#ifndef MAIN_LOKI_BLE_H
#define MAIN_LOKI_BLE_H

#include <zephyr/sys/atomic.h>

/* GATT service/characteristic UUIDs + BLE naming caps are generated from
 * interface/gatt.yaml by tools/gen_descriptors.py. Edit the YAML, not the
 * generated header. */
#include "loki_gatt.h"

 extern char ble_name[MAX_LEN_BLE_NAME+1]; // = e.g. "TREN0234" in   main_ble_utils.c
 extern char full_name[MAX_LEN_FULL_NAME+1]; // = e.g. "Keihan Otsu Line Type 700 [Sound! Euphonium] Wrapping Train 2023" in main_ble_utils.c
 extern uint16_t dcc_address; // main_loki.h

 const char *getBleShortName(void);
 const char *getBleLongName(void);
 int updateBleLongName(char *newName);
 int updateBleShortName(char *newName);
 int get_Eui64(char *printable);

void bt_submit_start_advertising_work();
void bt_submit_refresh_advertising_data_work();

/* BLE advertising intent flag for the lifecycle controller. Defined once in
 * main_ble_utils.c (single shared variable across the firmware). Other TUs
 * — notably main_ot_utils.c on SRP registration failure — can prefer the
 * higher-level `ble_lifecycle_force_recovery()` over poking this directly,
 * since the helper also reschedules the auto-stop timer. */
extern atomic_t ble_should_advertise;

extern void bt_notify_speed(void);
extern void bt_register(void);
extern int bt_ready(void);

/* Rename helpers — defined in main.c (where the loki settings handlers live).
 * Declared here so main_ble_utils.c's write_short_name / write_long_name
 * handlers don't fall back to an implicit-int declaration. Both are
 * read-only over the buffer. */
int modify_short_name(const char *buf, uint16_t len);
void modify_full_name(const char *buf, uint16_t len);

/* BLE advertising lifecycle controller.
 * Drives the "BLE off after a successful Thread attach" behavior governed by
 * CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES. Called from the OpenThread state
 * change callback and from the /ble-recovery CoAP resource.
 */
void ble_lifecycle_on_thread_attached(void);
void ble_lifecycle_on_thread_detached(void);
void ble_lifecycle_force_recovery(void);

/* Convenience for OpenThread / SRP code: re-open the BLE recovery window when
 * an SRP/DNS-SD operation fails, but only if CONFIG_LOKI_BLE_RECOVERY_ON_SRP_FAIL
 * is set. Centralises the gate so all SRP-failure call sites stay short and
 * the on/off decision lives in one place.
 *
 * Note: the explicit /ble-recovery CoAP endpoint deliberately uses the bare
 * ble_lifecycle_force_recovery() to bypass this gate — a human asking for
 * recovery should always get it. */
static inline void ble_lifecycle_recover_on_srp_failure(void)
{
	if (IS_ENABLED(CONFIG_LOKI_BLE_RECOVERY_ON_SRP_FAIL)) {
		ble_lifecycle_force_recovery();
	}
}

#endif /* MAIN_LOKI_BLE_H */