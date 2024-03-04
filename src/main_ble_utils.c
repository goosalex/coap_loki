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

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include "main_loki.h"


static struct bt_conn *default_conn;


/* Loki Service */
static struct bt_uuid_128 loki_service_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x01,0x00,
		0xbd,0xfc
	);
// Speed characteristic
static struct bt_uuid_128 loki_speed_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x02,0x00,
		0xbd,0xfc
			);
// Accelerate characteristic
static struct bt_uuid_128 loki_accelerate_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x03,0x00,
		0xbd,0xfc
	);
// PWM characteristic
static struct bt_uuid_128 loki_pwm_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x04,0x00,
		0xbd,0xfc
	);

// Direction characteristic
static struct bt_uuid_128 loki_direction_uuid =
	BT_UUID_INIT_128(
		0x35,0x0c,0x5a,0x49,0xa5,0x53,0xb7,0x99,0x87,0x43,0x25,0x5e,
		0x05,0x00,
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
				   );

static const struct bt_data ad[] = { BT_DATA_BYTES(
	BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)) };

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
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

void bt_notify_speed(void)
{

    bt_gatt_notify(NULL, &loki_service.attrs[1], &speed_value,
                   sizeof(speed_value));
}

int bt_ready(void)
{
	int err;

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return err;
	}

	printk("Advertising successfully started\n");
    return 0;
}

void bt_register(void)
{
    bt_conn_cb_register(&conn_callbacks);
}