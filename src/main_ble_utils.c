#include <zephyr/types.h>
#include <zephyr/toolchain.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
/* #include <sys/printk.h> 
#include <sys/byteorder.h> */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/settings/settings.h>
#include <hw_id.h>
#include "main_loki.h"
#include "main_ble_utils.h"
#include "displays/main_display.h"

// init global variables with default values
 char ble_name[MAX_LEN_BLE_NAME+1] = DEFAULT_NAME_PREFIX;
 char full_name[MAX_LEN_FULL_NAME+1]  = DEFAULT_NAME_PREFIX;
 

#include "main_ot_utils.h"

static struct bt_conn *default_conn;

LOG_MODULE_REGISTER(loki_ble, CONFIG_COAP_SERVER_LOG_LEVEL);

/* GATT service + characteristic UUID macros (LOKI_*_UUID_VAL / LOKI_*_UUID)
 * come from interface/gatt.yaml via tools/gen_descriptors.py — included
 * transitively through main_ble_utils.h → loki_gatt.h.
 *
 * See the OpenThread joiner-credential docs for the credential characteristic:
 *   https://openthread.io/guides/border-router/external-commissioning/prepare#prepare_the_joiner_device
 */



static ssize_t read_speed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &speed_value,
				 sizeof(speed_value));
}

static ssize_t write_speed(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	change_speed_directly(((uint8_t *)buf)[0]);

	return len;
}

static ssize_t read_pwm(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &pwm_base,
				 sizeof(pwm_base));
}

static ssize_t write_pwm(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
    uint16_t value;

    	if (len < sizeof(uint16_t)) {
    		value = *(uint8_t *)buf;
    	} else {
    		value = sys_get_le16(buf);
    	}


    printk("Set PWM Base to %uHz", value);
	change_pwm_base(value);

	return len;
}

static ssize_t read_direction(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &direction_pattern,
				 sizeof(speed_value));
}

static ssize_t write_direction(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
    uint8_t new_pattern = ((uint8_t *)buf)[0];

	change_direction(new_pattern);

	return len;
}

static ssize_t read_accelation(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &accel_order,
				 sizeof(accel_order));
}

static ssize_t write_accelation(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	speed_set_acceleration(((int8_t *)buf)[0]);
	notify_motion_change();
	return len;
}

static void speed_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				     uint16_t value)
{
	speed_notify_enabled = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
	if (speed_notify_enabled) {
		printk("Speed Notifications were ENABLED by GATT Client\n");
	} else {
		printk("Speed Notifications were DISABLED by GATT Client\n");
	}
}

static _ssize_t read_name(struct bt_conn *conn, const struct bt_gatt_attr *attr,
              void *buf, uint16_t len, uint16_t offset)
{
    const char *name = getBleLongName();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, name, strlen(name));
}

static _ssize_t write_name(struct bt_conn *conn,
               const struct bt_gatt_attr *attr, const void *buf,
               uint16_t len, uint16_t offset, uint8_t flags)
{
    int err;

		/* +1 leaves room for the NUL terminator written below — without it,
		 * a write of exactly MAX_LEN_FULL_NAME bytes overflowed by one. */
		char new_name[MAX_LEN_FULL_NAME + 1];
    if (len > MAX_LEN_FULL_NAME) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(new_name, buf, len);
    new_name[len] = '\0';

    err = updateBleLongName(new_name);
    if (err) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}

static _ssize_t read_ble_name(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	const char *name = getBleShortName();
	return bt_gatt_attr_read(conn, attr, buf, len, offset, name, strlen(name));
}


static _ssize_t write_ble_name(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	// The name is limited to 8 characters, no null terminator
	if (len > MAX_LEN_BLE_NAME ) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	int err = modify_short_name(buf, len);
	if (err) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return len;
}
// TODO: Implement the DCC characteristic
static ssize_t read_dcc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &dcc_address,
				 sizeof(dcc_address));
}

static ssize_t write_dcc(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	uint16_t value;
	if (len < sizeof(uint16_t)) {
		value = *(uint8_t *)buf;
	} else {
		value = sys_get_le16(buf);
	}
	apply_dcc_address(value);
	return len;
}

static ssize_t read_credential(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	int err = 0;
	char eui64[8*2+1];
	err = get_Eui64(eui64);
	if (err) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	return bt_gatt_attr_read(conn, attr, buf, len, offset, eui64, strlen(eui64));
}

static ssize_t write_credential(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	int err = 0;
	char cred[len+1];
	memcpy(cred, buf, len);
	cred[len] = '\0';
	err = start_thread_joiner(cred);
	if (err) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	return len;
}

/* Loki Service Declaration */
BT_GATT_SERVICE_DEFINE(
    loki_service, BT_GATT_PRIMARY_SERVICE(LOKI_SERVICE_UUID),
    // Acceleration Characteristic
    // Properties: Read, Write
    BT_GATT_CHARACTERISTIC(LOKI_ACCELERATE_UUID,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                read_accelation, write_accelation,
                   &accel_order),
    // Speed Characteristic
    // Properties: Read, Write, Notify
    BT_GATT_CHARACTERISTIC(LOKI_SPEED_UUID,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                   read_speed, write_speed, &speed_value),
    // Speed Change  CCCD (used for Notifications and Indications)
     BT_GATT_CCC(speed_ccc_cfg_changed,
            BT_GATT_PERM_READ | BT_GATT_PERM_WRITE) ,

    BT_GATT_CHARACTERISTIC(LOKI_PWM_UUID,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                   read_pwm, write_pwm, &pwm_base),

    BT_GATT_CHARACTERISTIC(LOKI_DIRECTION_UUID,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                   read_direction, write_direction, &direction_pattern),

    BT_GATT_CHARACTERISTIC(LOKI_NAME_UUID,
                     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_name, write_name, NULL),    
  
    BT_GATT_CHARACTERISTIC(LOKI_DCC_UUID,
                     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_dcc, write_dcc, NULL),    

    // Setting the credential initiates a joiner procedure, reading you'll obtain the EUI64
    BT_GATT_CHARACTERISTIC(LOKI_CREDENTIAL_UUID,
                     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_credential, write_credential, NULL),    	

    BT_GATT_CHARACTERISTIC(LOKI_BLE_NAME_UUID,
                     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_ble_name, write_ble_name, NULL)  

                   );				   

/* Advertising data */
// Problem: The payload is limited to 31 bytes, so the name shound not be too long
// and as this is a custom service, a 16 Byte UUID is needed
// Solution: Use the short name and the scan response to send the full name
// Problem: The scan response is not always sent, so the name may not be updated 

static struct bt_data ad[3] = { BT_DATA_BYTES(
    BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)), // 3 Bytes (length,type and value Byte)
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, LOKI_SERVICE_UUID_VAL), // 18 Bytes
    BT_DATA(BT_DATA_NAME_SHORTENED, ble_name, MAX_LEN_BLE_NAME), };  // 31 Bytes - 3 - 18 - 2 = 8 Bytes left for the short name
	// positional index of short name in "ad" advertisement data array structure. Used for further updates.
#define BLE_ADV_DATA_NAME_IDX 2


static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		default_conn = bt_conn_ref(conn);
		display_updateBTConnectionStatus("GATT");
		printk("Connected\n");
	}
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
	display_updateBTConnectionStatus("");
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	// The names might have changed, so update the advertising data
	// bt_submit_refresh_advertising_data_work();
	// After disconnect, advertising might not be active — but only resume
	// if the lifecycle controller still wants us to advertise. Once Thread
	// has been attached long enough for ble_stop_handler to fire, we leave
	// BLE off until Thread detaches or /ble-recovery is invoked.
	if (atomic_get(&ble_should_advertise)) {
		bt_submit_start_advertising_work();
	}
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
};

#define DEVICE_NAME		CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)

static struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};


 int updateBleLongName(char *newName) {
  int err;
  
  // Update the device name
  printk("Set new name: %s\n",newName);
  err = bt_set_name(newName);
  if(err) {
    printk("Error setting device name: %d\n", err);
  } else {
printk("Changed device name to: %s\n", newName);
    // Update the advertising and scan response data needed to update the advertised device name
    // Only need to modify the scan response data in this example as name is in scan response here.
    sd->data = newName;
    sd->data_len = strlen(newName);

    if(err) {
      printk("Error setting advertised names: %d\n", err);
	  /* -11 means: not currently advertising
	  	if (!atomic_test_bit(adv->flags, BT_ADV_ENABLED)) {
		return -EAGAIN;
	}
	  */
    } else {
      printk("Changed advertised long name to: %s\n", newName);
    }
  }
  return err;
}

 int updateBleShortName(char *newName) {
  int err = 0;
  /* Advertising data */
// Problem: The payload is limited to 31 bytes, so the name shound not be too long
// and as this is a custom service, a 16 Byte UUID is needed
// Solution: Use the short name and the scan response to send the full name
// Problem: The scan response is not always sent, so the name may not be updated 


  // Update the device name
  LOG_INF("Set new advertised name: %s\n",newName);

	int ad_name_idx = BLE_ADV_DATA_NAME_IDX;
	ad[ad_name_idx].data = (const uint8_t *)newName;
	ad[ad_name_idx].data_len = strlen(newName);
	

  return err;
}

void start_advertising(struct k_work *work) {
  int err;
  static const struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0,
		.secondary_max_skip = 0,
		.options = BT_LE_ADV_OPT_CONN,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};

  if (!atomic_get(&ble_should_advertise)) {
	LOG_DBG("start_advertising skipped: BLE intentionally off");
	return;
  }

  err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
	LOG_ERR("Advertising failed to start (err %d)\n", err);
	
  } else {
	LOG_INF("Advertising successfully started\n");	
  }

  
}
K_WORK_DEFINE(start_advertising_work, start_advertising);

/* -- BLE advertising lifecycle controller ------------------------------------
 * Default state on boot is "advertise". On a successful Thread attach the
 * stop work is scheduled for CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES, after
 * which advertising is halted. A Thread detach cancels the pending stop and
 * resumes advertising. A CoAP PUT to /ble-recovery (see loki_coap_utils) or a
 * call to ble_lifecycle_force_recovery() from any TU (e.g. OpenThread code on
 * SRP registration failure) forces a fresh window. Setting the Kconfig value
 * to 0 disables the feature.
 *
 * `ble_should_advertise` is the single shared atomic backing the intent flag,
 * declared `extern` in main_ble_utils.h so other TUs may inspect it directly.
 * Prefer the ble_lifecycle_* API where possible — those helpers also drive
 * the stop work, which a raw atomic flip won't.
 */
atomic_t ble_should_advertise = ATOMIC_INIT(1);

static void ble_stop_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_get(&ble_should_advertise)) {
		return; /* already stopped, or recovery flipped intent back on */
	}
	LOG_INF("Thread attached for >%d min, stopping BLE advertising",
		CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES);
	atomic_set(&ble_should_advertise, 0);
	int err = bt_le_adv_stop();
	if (err && err != -EALREADY) {
		LOG_WRN("bt_le_adv_stop returned %d", err);
	}
	display_updateBTConnectionStatus("off");
}
static K_WORK_DELAYABLE_DEFINE(ble_stop_work, ble_stop_handler);

void ble_lifecycle_on_thread_attached(void)
{
	if (CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES == 0) {
		return; /* feature disabled */
	}
	LOG_INF("Thread attached; BLE advertising will stop in %d min",
		CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES);
	k_work_reschedule(&ble_stop_work,
			  K_MINUTES(CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES));
}

void ble_lifecycle_on_thread_detached(void)
{
	int was_pending = k_work_cancel_delayable(&ble_stop_work);
	atomic_val_t prev = atomic_set(&ble_should_advertise, 1);
	if (prev == 0) {
		LOG_INF("Thread detached; resuming BLE advertising "
			"(stop-timer %s)",
			was_pending > 0 ? "was pending" : "not pending");
		bt_submit_start_advertising_work();
	} else if (was_pending > 0) {
		LOG_INF("Thread detached; cancelled pending BLE stop");
	}
}

void ble_lifecycle_force_recovery(void)
{
	if (CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES == 0) {
		LOG_INF("BLE recovery requested but feature is disabled");
		return;
	}
	LOG_INF("BLE recovery requested; (re-)opening advertising window for "
		"%d min", CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES);
	atomic_val_t prev = atomic_set(&ble_should_advertise, 1);
	if (prev == 0) {
		bt_submit_start_advertising_work();
	}
	k_work_reschedule(&ble_stop_work,
			  K_MINUTES(CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES));
}
/* ---------------------------------------------------------------------------*/

 const char *getBleLongName(void) {
  return bt_get_name();
}



void refresh_advertising_data(struct k_work *work){
  int err;
  err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if(err) {
	LOG_ERR("Error updating advertising data: %d\n", err);
  } else {
	LOG_INF("Updated advertising data\n");
  }
}

K_WORK_DEFINE(refresh_advertising_data_work, refresh_advertising_data);

void bt_submit_start_advertising_work() {
  k_work_submit(&start_advertising_work);
}
void bt_submit_refresh_advertising_data_work() {
  k_work_submit(&refresh_advertising_data_work);
}


 const char *getBleShortName(void) {
	int ad_name_idx = BLE_ADV_DATA_NAME_IDX;
  return (const char *)ad[ad_name_idx].data;
}

void bt_notify_speed(void)
{
	/* Locate the Speed value attribute by UUID rather than by index, so any
	 * reordering of characteristics in BT_GATT_SERVICE_DEFINE doesn't
	 * silently break notifications. The previous `&attrs[1]` pointed at
	 * the Acceleration characteristic declaration once the service grew
	 * past one entry. */
	bt_gatt_notify_uuid(NULL, LOKI_SPEED_UUID, loki_service.attrs,
			    &speed_value, sizeof(speed_value));
}

int bt_ready(void)
{
	int err;
	static const struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0,
		.secondary_max_skip = 0,
		.options = 0,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};
	
	bt_le_adv_stop();
	err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)\n", err);
		return err;
	}

	printk("Advertising successfully started\n");
    return 0;
}

void bt_register(void)
{
    bt_conn_cb_register(&conn_callbacks);
	LOG_INF("Bluetooth callbacks registered\n");
}