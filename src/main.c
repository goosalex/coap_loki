/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
#include <openthread/coap.h>

#include <zephyr/drivers/pwm.h>
#include <stdio.h>

#include <zephyr/net/openthread.h>
#include <openthread/thread.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>

#include <dk_buttons_and_leds.h>
#include "main_ble_utils.h"
#include "loki_coap_utils.h"
#include "main_ot_utils.h"
#include "main_loki.h"

#if MOTOR_DRV8871
#include "motors/motorDRV8871.h"
#elif MOTOR_TB6612		
#include "motors/motorTB6612driver.c"
#else
#include "motors/motorTB67driver.c"
#endif

#define OT_CONNECTION_LED 3


/*
 * Devicetree helper macro which gets the 'flags' cell from a 'gpios'
 * property, or returns 0 if the property has no 'flags' cell.
 */
#define FLAGS_OR_ZERO(node)						\
	COND_CODE_1(DT_PHA_HAS_CELL(node, gpios, flags),		\
		    (DT_GPIO_FLAGS(node, gpios)),			\
		    (0))

// LED 1 Defintions
/* The devicetree node identifier for the "led0" alias - which corresponds to LED 1 on the board. 
#define LED1_NODE DT_NODELABEL(led0)
#if !DT_NODE_EXISTS(DT_NODELABEL(led0))
#error "Overlay for LED0 node not properly defined."
#endif
*/
/*
// LED 2 Defintions - temporary replacement for speed PWM
#define LED2_NODE DT_NODELABEL(led2)
#if !DT_NODE_EXISTS(DT_NODELABEL(led2))
#error "Overlay for LED2 node not properly defined."
#endif


#if !( DT_NODE_HAS_STATUS(LED1_NODE, okay) && DT_NODE_HAS_STATUS(LED2_NODE, okay))
#error "LED1 or LED2 node not okay."

#endif
*/

/* First Demo uses all other 3 LEDs
static const struct gpio_dt_spec led2_switch =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(led1_switch), gpios, {0});
*/

/*
 * Get button configuration from the devicetree sw0 alias.
 *
 * At least a GPIO device and pin number must be provided. The 'flags'
 * cell is optional.
 */

#define BUTTON1_NODE DT_ALIAS(sw0)

#if DT_NODE_HAS_STATUS(BUTTON1_NODE, okay)

#else
#error "Unsupported board: sw0 devicetree alias is not defined"

#endif





LOG_MODULE_REGISTER(loki_main, CONFIG_COAP_SERVER_LOG_LEVEL);


void init_srp() ;




uint8_t speed_notify_enabled;




void notify_speed_change()
{
		// Notify if Notifications are enabled
	if (speed_notify_enabled) {
		LOG_DBG("Sending Speed Notifications to GATT Client\n");
		/* original demo code:
				bt_gatt_notify(NULL, &simple_service.attrs[1], &button_1_value,
			       sizeof(button_1_value));
		*/
		// TODO: evaluate ret value, print error , if any

		bt_notify_speed();
	}

}



void stop_motor()
{
	change_speed_directly(0);
}



void init_default_settings()
{
	speed_value = 0;
	accel_order = 0;
	direction_pattern = 0;
	speed_notify_enabled = 0;
	change_pwm_base(1000);
	int err;
	
	char buf[HW_ID_LEN] = "unsupported";
	err = hw_id_get(buf, ARRAY_SIZE(buf));
		if (err) {
			LOG_ERR("hw_id_get failed (err %d)\n", err);		
	}



	// Set the default name
    // Ensure the buffer is large enough to hold the new name
    char ble_name[MAX_LEN_BLE_NAME + 1]; // "LOKI" + 4 characters from buf + null terminator

    // Copy "LOKI" to ble_name
    strcpy(ble_name, "LOKI");

    // Concatenate the first 4 characters from buf to ble_name
    strncat(ble_name, buf, MAX_LEN_BLE_NAME - strlen(ble_name));

    // Ensure null termination
    ble_name[8] = '\0';

	strcpy(full_name,ble_name);

	dcc_address = 0;
}



otSrpClientBuffersServiceEntry *register_service( otInstance *p_instance ,  char *instance_name, char *service_name);


void settings_handle_commit(void){
	LOG_INF("Settings<LOKI> loaded\n");
}

int settings_handle_set(const char *name, size_t len_rd, settings_read_cb read_cb,
			void *cb_arg)
{
	if (!name) {
		return -EINVAL;
	}

	if (!strcmp(name, "loki/shortname")) {
		ssize_t len = read_cb(cb_arg, &ble_name, sizeof(ble_name));
		if (len < 0) {
			LOG_ERR("Failed to read shortname from storage (err %zd)\n", len);
			return len;
		}
		ble_name[len] = '\0';
	} else if (!strcmp(name, "loki/longname")) {
		ssize_t len = read_cb(cb_arg, &full_name, sizeof(full_name));
		if (len < 0) {
			LOG_ERR("Failed to read longname from storage (err %zd)\n", len);
			return len;
		}
		full_name[len] = '\0';
	} else if (!strcmp(name, "loki/dcc")) {
		ssize_t len = read_cb(cb_arg, &dcc_address, sizeof(dcc_address));
		if (len < 0) {
			LOG_ERR("Failed to read dcc from storage (err %zd)\n", len);
			return len;
		}
	} else {
		return -ENOENT;
	}

	return 0;
}

int settings_handle_export(int (*cb)(const char *name,
			       const void *value, size_t val_len))
{	
	(void)cb("loki/shortname", &ble_name, sizeof(ble_name));
	(void)cb("loki/longname", &full_name, sizeof(full_name));
	(void)cb("loki/dcc", &dcc_address, sizeof(dcc_address));
	return 0;
}

struct settings_handler loki_settings_handler = {
	.name = "loki",
	.h_set = settings_handle_set,
//	.h_commit = settings_handle_commit,
	.h_export = settings_handle_export
};

void load_settings()
{
	int rc;
	// Load the settings from the flash
	rc = settings_subsys_init();
	if (rc) {
		LOG_ERR("settings subsys initialization: fail (err %d)\n", rc);
		return;
	}

	LOG_INF("settings subsys initialization: OK.\n");

	rc = settings_register(&loki_settings_handler);
}


void *de_register_service( otSrpClientService service){
	otError error;
	otInstance *p_instance = openthread_get_default_instance();
	error = otSrpClientRemoveService(p_instance, &service);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Cannot remove service: %s", otThreadErrorToString(error));
		return NULL;
	}
	return NULL;
}

void modify_full_name(char *buf, uint16_t len)
{
	if (strncmp(buf, full_name, len) == 0) {
		return;
	} else {
		LOG_INF("Changing full name to %s\n", (buf));
		de_register_service(long_name_coap_service);
	}

	if (len < MAX_LEN_FULL_NAME) {
		strncpy(full_name, buf, len);
		full_name[len] = '\0';
	} else {
		strncpy(full_name, buf, MAX_LEN_FULL_NAME);
		full_name[MAX_LEN_FULL_NAME] = '\0';
	}

	init_srp();

	// TODO: Implement, reset BLE Adv Name and restart BLE and CoAP
}

void modify_short_name(char *buf, uint16_t len)
{
	if (strncmp(buf, ble_name, len) == 0) {
		return;
	} else {
		LOG_INF("Changing short name to %s\n", (buf));
		de_register_service(short_name_coap_service);
	}

	if (len < MAX_LEN_BLE_NAME) {
		strncpy(ble_name, buf, len);
		ble_name[len] = '\0';
	} else {
		strncpy(ble_name, buf, MAX_LEN_BLE_NAME);
		ble_name[MAX_LEN_BLE_NAME] = '\0';
	}
	
	updateBleShortName(ble_name);
	init_srp();
}





int main(void)
{
	int err;
	printk("Startup\r");
	LOG_INF("%s","Startup Information:\n");
	if (motor_init() != 0 ) {
		LOG_ERR("Motor init failed\n");
		return -1;
	}
dk_set_led_on(OT_CONNECTION_LED);
dk_set_led_on(0);
dk_set_led_on(1);
dk_set_led_on(2);
	load_settings();

	// TODO : Check if otDatasetIsCommissioned. Only start Thread IF already commissioned (avoid automatic commissioning after reset)
	// to decommission without reset/reboot, set something in dataset invalid/clear name/key ?
	if (otDatasetIsCommissioned( openthread_get_default_context() ) == true) {
		LOG_INF("Thread already commissioned\n");
	
		if (loki_coap_init(
			change_speed_directly,
			speed_set_acceleration,
			change_direction,
			stop_motor,
			modify_full_name
			) != 0) {
				LOG_ERR("CoAP init failed\n");			
			} else {
				enable_thread();
				LOG_INF("Thread enabled\n");
				init_srp();
				LOG_INF("SRP client enabled\n");
			}

	} else {
		LOG_INF("Thread not commissioned\n");
	}
	LOG_INF("Starting BLE\n");
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return -2;
	}
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
			if (err) {
			LOG_WRN("Bluetooth settings load failed (err %d)\n", err);
			return -3;
		}
	}	
	if (bt_ready() != 0) {
		LOG_ERR("Bluetooth setup failed\n");
		return -4;
	}
	bt_register();	
	return 0;

}
