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

#include "main_ot_utils.h"

static struct bt_conn *default_conn;

LOG_MODULE_REGISTER(loki_ble, CONFIG_COAP_SERVER_LOG_LEVEL);

/* Loki Service */
static struct bt_uuid_128 loki_service_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x01,0x00,
		0xbd,0xfc
	);
// loki_service_uuid formatted as UUID is fcbd0001-5e25-4387-99b7-53a5495a0c35

// Speed characteristic
// formatted as UUID is fcbd0002-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_speed_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x02,0x00,
		0xbd,0xfc
			);
// Accelerate characteristic
// formatted as UUID is fcbd0003-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_accelerate_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x03,0x00,
		0xbd,0xfc
	);
// PWM characteristic
// formatted as UUID is fcbd0004-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_pwm_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x04,0x00,
		0xbd,0xfc
	);

// Direction characteristic
// formatted as UUID is fcbd0005-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_direction_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x05,0x00,
		0xbd,0xfc
	);

// Long Name characteristic
// formatted as UUID is fcbd0006-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_name_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x06,0x00,
		0xbd,0xfc
	);

// virtual (DCC) address characteristic
// formatted as UUID is fcbd0007-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_dcc_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x07,0x00,
		0xbd,0xfc
	);

// OpenThread  Joiner Credential
// see https://openthread.io/guides/border-router/external-commissioning/prepare#prepare_the_joiner_device
// formatted as UUID is fcbd000a-5e25-4387-99b7-53a5495a0c35
static struct bt_uuid_128 loki_credential_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x0a,0x00,
		0xbd,0xfc
	);	

static struct bt_uuid_128 loki_ble_name_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x0b,0x00,
		0xbd,0xfc
	);	



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
	notify_speed_change();
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
    char *name = getBleLongName();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, name, strlen(name));                     
}

static _ssize_t write_name(struct bt_conn *conn,
               const struct bt_gatt_attr *attr, const void *buf,
               uint16_t len, uint16_t offset, uint8_t flags)
{
    int err;
	
		char new_name[MAX_LEN_FULL_NAME];
    if (len > sizeof(new_name)) {
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
	char *name = getBleShortName();
	return bt_gatt_attr_read(conn, attr, buf, len, offset, name, strlen(name));                     
}


static _ssize_t write_ble_name(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	int err;
	char new_name[MAX_LEN_BLE_NAME];
	if (len > sizeof(new_name)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(new_name, buf, len);
	new_name[len] = '\0';

	err = updateBleShortName(new_name);
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
	u_int16_t value;
	if (len < sizeof(uint16_t)) {
		value = *(uint8_t *)buf;
	} else {
		value = sys_get_le16(buf);
	}
	dcc_address = value;
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
	loki_service, BT_GATT_PRIMARY_SERVICE(&loki_service_uuid),
	// Acceleration Characteristic
	// Properties: Read, Write
	BT_GATT_CHARACTERISTIC(&loki_accelerate_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
				read_accelation, write_accelation,
			       &accel_order),
	// Speed Characteristic
	// Properties: Read, Write, Notify
	BT_GATT_CHARACTERISTIC(&loki_speed_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_speed, write_speed, &speed_value),
	// Speed Change  CCCD (used for Notifications and Indications)
	 BT_GATT_CCC(speed_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE) ,

    BT_GATT_CHARACTERISTIC(&loki_pwm_uuid.uuid,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                   read_pwm, write_pwm, &pwm_base),

    BT_GATT_CHARACTERISTIC(&loki_direction_uuid.uuid,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                   read_direction, write_direction, &direction_pattern),

    BT_GATT_CHARACTERISTIC(&loki_name_uuid.uuid,
                     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_name, write_name, NULL),    
  
    BT_GATT_CHARACTERISTIC(&loki_dcc_uuid.uuid,
                     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_dcc, write_dcc, NULL),    

	// Setting the credential initiates a joiner procedure, reading you'll obtain the EUI64
    BT_GATT_CHARACTERISTIC(&loki_credential_uuid.uuid,
            	     BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     read_credential, write_credential, NULL),    	

    BT_GATT_CHARACTERISTIC(&loki_ble_name_uuid.uuid,
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
	BT_DATA(BT_DATA_UUID128_ALL, loki_service_uuid.val, 16), // 18 Bytes
	BT_DATA(BT_DATA_NAME_SHORTENED, "LOKI", 4), };  // 31 Bytes - 3 - 18 - 2 = 8 Bytes left for the short name
	// positional index of short name in "ad" advertisement data array structure. Used for further updates.
	#define BLE_ADV_DATA_NAME_IDX 2;


static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		default_conn = bt_conn_ref(conn);
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);

	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	// The names might have changed, so update the advertising data
	bt_submit_refresh_advertising_data_work();
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
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
  int err;
  /* Advertising data */
// Problem: The payload is limited to 31 bytes, so the name shound not be too long
// and as this is a custom service, a 16 Byte UUID is needed
// Solution: Use the short name and the scan response to send the full name
// Problem: The scan response is not always sent, so the name may not be updated 


  // Update the device name
  LOG_INF("Set new advertised name: %s\n",newName);

	int ad_name_idx = BLE_ADV_DATA_NAME_IDX;
	ad[ad_name_idx].data = newName;
	ad[ad_name_idx].data_len = strlen(newName);

    if(err) {
      LOG_ERR("Error saving advertised names: %d\n", err);
    } else {
      LOG_INF("Changed advertised long name to: %s\n", newName);
    }  
  return err;
}
void start_advertising(struct k_work *work) {
  int err;
  err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd)); // ,NULL, 0);
  if (err) {
	LOG_ERR("Advertising failed to start (err %d)\n", err);
	
  } else {
	LOG_INF("Advertising successfully started\n");	
  }

  
}
K_WORK_DEFINE(start_advertising_work, start_advertising);

 char *getBleLongName() {
  char *name = bt_get_name();
  return name;
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


 char *getBleShortName() {
	int ad_name_idx = BLE_ADV_DATA_NAME_IDX;
  return ad[ad_name_idx].data;
}

void bt_notify_speed(void)
{

    bt_gatt_notify(NULL, &loki_service.attrs[1], &speed_value,
                   sizeof(speed_value));
}

int bt_ready(void)
{
	int err;	
	bt_le_adv_stop();
	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd)); // ,NULL, 0);
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