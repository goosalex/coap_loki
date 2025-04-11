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
#include <zephyr/drivers/hwinfo.h>
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

#ifdef CONFIG_LVGL
// Start Display related includes
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>
// End Display related includes
#endif

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
	int EUI64_LEN = 8;
	
	int id_len;
	uint8_t eui64buf[EUI64_LEN];
	// certainly depends on CONFIG_HWINFO_NRF (or other if other device is chosen) in prj.conf
	id_len = hwinfo_get_device_id(&eui64buf, EUI64_LEN);
	if (id_len < 0) {
		LOG_ERR("Failed to get EUI64 from HWINFO (err %d)\n", id_len);
		return;
	}
	int id_str_len = id_len*2;
	// convert eui64 to hex string
	char buf[id_str_len+1];
	for (int i = 0; i < id_len; i++) {
		sprintf(buf + i * 2, "%02X", eui64buf[i]);
	}
	LOG_INF("EUI64_ID: %s\n",buf);


	// Set the default name

    // Copy "LOKI"/"TREN" etc to ble_name
	char default_name[] = DEFAULT_NAME_PREFIX;
    strcpy(ble_name, default_name);

	int start_indx = 0;
	if (strlen(ble_name) + id_str_len > MAX_LEN_BLE_NAME) {
		start_indx = id_str_len - (MAX_LEN_BLE_NAME - strlen(ble_name));
	}
    // Concatenate the last 4 characters from buf to ble_name
    strncat(ble_name, &buf[start_indx], id_str_len - start_indx);

    // Ensure null termination
    ble_name[strlen(ble_name)] = '\0';

	strcpy(full_name,ble_name);

	dcc_address = 0;
}






void settings_handle_commit(void){
	LOG_INF("Settings<LOKI> loaded\n");
}

// handler for settings that start with "loki/"
int loki_settings_handle_set(const char *name, size_t len_rd, settings_read_cb read_cb,
			void *cb_arg)
{
	if (!name) {
		return -EINVAL;
	}

	if (!strcmp(name, "shortname")) {
		ssize_t len = read_cb(cb_arg, &ble_name, sizeof(ble_name));
		if (len < 0) {
			LOG_ERR("Failed to read shortname from storage (err %zd)\n", len);
			return len;
		}
		ble_name[len] = '\0';
	} else if (!strcmp(name, "longname")) {
		ssize_t len = read_cb(cb_arg, &full_name, sizeof(full_name));
		if (len < 0) {
			LOG_ERR("Failed to read longname from storage (err %zd)\n", len);
			return len;
		}
		full_name[len] = '\0';
	} else if (!strcmp(name, "dcc")) {
		ssize_t len = read_cb(cb_arg, &dcc_address, sizeof(dcc_address));
		if (len < 0) {
			LOG_ERR("Failed to read dcc from storage (err %zd)\n", len);
			return len;
		}
	} else {
		LOG_ERR("Unknown setting: %s\n", name);
		return -ENOENT;
	}
	LOG_INF("Setting<LOKI> %s loaded\n", name);
	return 0;
}

// Flag to check if settings have ever been initialized
static bool settings_initialized_flag = false;
// Callback to load the settings_initialized_flag in a blocking manner from storage. Fails(is not called) if settings_initialized_flag is not found in storage
int settings_initialized_flag_loader_direct_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param){
	if (!strcmp(key, "loki/init")) {
		ssize_t len = read_cb(cb_arg, &settings_initialized_flag, sizeof(settings_initialized_flag));
		if (len < 0) {
			LOG_ERR("Failed to read settings_initialized_flag from storage (err %zd)\n", len);
			return len;
		}
		settings_initialized_flag = true;
	}
	return 0;
}

// Callback to export the settings to storage
int settings_handle_export(int (*cb)(const char *name,
			       const void *value, size_t val_len))
{	
	(void)cb("loki/shortname", &ble_name, sizeof(ble_name));
	(void)cb("loki/longname", &full_name, sizeof(full_name));
	(void)cb("loki/dcc", &dcc_address, sizeof(dcc_address));
	(void)cb("loki/init", &settings_initialized_flag, sizeof(bool));
	return 0;
}

struct settings_handler loki_settings_handler = {
	.name = DEFAULT_NAME_PREFIX,
	.h_set = loki_settings_handle_set,
//	.h_commit = settings_handle_commit,
	.h_export = settings_handle_export
};

void load_settings_from_nvm()
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
	if (rc) {
		LOG_ERR("settings_register: fail (err %d)\n", rc);
		return;
	}
	rc = settings_load_subtree_direct("loki/init", settings_initialized_flag_loader_direct_cb, NULL);
	if (rc) {
		LOG_ERR("Check if App settings have ever been initialized: settings_load_subtree_direct: failed (err %d)\n", rc);
		return;
	}
	if (!settings_initialized_flag) {
		LOG_INF("Settings have never been initialized, setting defaults\n");
		init_default_settings();
		rc = settings_save_subtree("loki");
		if (rc) {
			LOG_ERR("Error saving settings to NVM: %d\n", rc);
		} else {
			LOG_INF("Saved settings to NVM\n");
		}
		settings_initialized_flag = true;
		rc = settings_save_subtree("loki/init");
		if (rc) {
			LOG_ERR("Error saving first initialization flag settings_initialized_flag to NVM: %d\n", rc);
		} else {
			LOG_INF("Saved settings_initialized_flag to NVM\n");
		}
	} else {
		LOG_INF("Settings have been initialized\n");
	}
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
		re_register_coap_service(openthread_get_default_instance(), &long_name_coap_service, buf, SRP_LONGNAME_SERVICE);
	}

	if (len < MAX_LEN_FULL_NAME) {
		strncpy(full_name, buf, len);
		full_name[len] = '\0';
	} else {
		strncpy(full_name, buf, MAX_LEN_FULL_NAME);
		full_name[MAX_LEN_FULL_NAME] = '\0';
	}

	updateBleLongName(full_name);

	
}

int modify_short_name(char *buf, uint16_t len)
{
	if (strncmp(buf, ble_name, len) == 0) {
		return;
	} else {
		LOG_INF("Changing short name to %s\n", (buf));
		re_register_coap_service(openthread_get_default_instance(), &short_name_coap_service, buf, SRP_SHORTNAME_SERVICE);
	}

	if (len <= MAX_LEN_BLE_NAME) {
		memcpy(&ble_name, buf, len);
		ble_name[len] = '\0';
	} else {
		memcpy(&ble_name, buf, MAX_LEN_BLE_NAME);
		ble_name[MAX_LEN_BLE_NAME] = '\0';
	}
	int ret = settings_save_subtree("loki/shortname");
	if (ret) {
		LOG_ERR("Error saving shortname setting to NVM: %d\n", ret);
	} else {
		LOG_INF("Saved short name to NVM: %s\n", ble_name);
	}
	LOG_INF("Change of advertised short name will happen on next disconnect\n", ble_name);
	return updateBleShortName(&ble_name);
}

# ifdef CONFIG_LVGL

char count_str[11] = {0};
const struct device *display_dev;
lv_obj_t *hello_world_label;


void lvgl_init(void)
{
	lv_init();	

	// lvgl_init_input
	// lvgl_init_display

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}
	hello_world_label = lv_label_create(lv_scr_act());
	lv_label_set_text(hello_world_label, "Hella world!");
	lv_obj_set_style_text_font(hello_world_label, &lv_font_unscii_8, 0);
	int32_t h = lv_obj_get_height(hello_world_label);
	lv_obj_align(hello_world_label, LV_ALIGN_TOP_LEFT, 0, 0);
	
	lv_task_handler();
	display_blanking_off(display_dev);


}
#endif

void init_display(void)
{
#ifdef CONFIG_LVGL
	lvgl_init();
#endif
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
init_default_settings();


	init_display();

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		load_settings_from_nvm();
		err = settings_load();
			if (err) {
			LOG_WRN("Bluetooth and other settings load failed (err %d)\n", err);
			return -3;
		}
	}	
	
	// TODO : Check if otDatasetIsCommissioned. Only start Thread IF already commissioned (avoid automatic commissioning after reset)
	// to decommission without reset/reboot, set something in dataset invalid/clear name/key ?
	if (otDatasetIsCommissioned( openthread_get_default_instance() ) == true) {
		LOG_INF("Thread already commissioned\n");
		enable_thread();
		LOG_INF("Thread enabled\n");
		init_srp();				
		LOG_INF("SRP client enabled\n");
		if (loki_coap_init(
			change_speed_directly,
			speed_set_acceleration,
			change_direction,
			stop_motor,
			modify_full_name
			) != 0) {
				LOG_ERR("CoAP init failed\n");			
			} else {
				LOG_INF("CoAP initialized\n");
				if (short_name_coap_service.mService.mInstanceName != NULL) {
					LOG_INF("Service %s already registered as %s, freeing first", SRP_SHORTNAME_SERVICE, ble_name);
					otSrpClientBuffersFreeService(openthread_get_default_instance(), &short_name_coap_service);
				}
				if (long_name_coap_service.mService.mInstanceName != NULL) {
					LOG_INF("Service %s already registered as %s, freeing first", SRP_LONGNAME_SERVICE, full_name);
					otSrpClientBuffersFreeService(openthread_get_default_instance(), &long_name_coap_service);
				}
				register_coap_service(openthread_get_default_instance(), full_name, SRP_LONGNAME_SERVICE);
				register_coap_service(openthread_get_default_instance(), ble_name, SRP_SHORTNAME_SERVICE);
			}
		if (dcc_address != 0) {
			LOG_INF("DCC Address set to %d\n", dcc_address);
			if (&dcc_name_coap_service != NULL) {
				LOG_INF("Service %s already registered as %d, freeing first", SRP_DCC_SERVICE, dcc_address);
				otSrpClientBuffersFreeService(openthread_get_default_instance(), &dcc_name_coap_service);
			}
			char *dcc_string = malloc(15);
			sprintf(dcc_string, "%d", dcc_address);
			dcc_name_coap_service = *register_service(openthread_get_default_instance(),dcc_string , SRP_LCN_SERVICE, SRP_LCN_PORT);

			bindUdpHandler(openthread_get_default_instance(),&loconet_udp_socket, SRP_LCN_PORT, on_udp_loconet_receive);
			LOG_INF("UDP Port %d is listening for LNet Messages addressing #%s",SRP_LCN_PORT,dcc_string);
		}

	} else {
		LOG_INF("Thread not commissioned\n");
	}
	LOG_INF("Starting BLE\n");
	//settings_load_subtree("bt");
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth enable failed (err %d)\n", err);
		return -2;
	}
	settings_load_subtree("bt");
	settings_load_subtree("loki");
	updateBleShortName(ble_name);
	updateBleLongName(full_name);
	/*err = bt_ready();
	#if (err) {
		LOG_ERR("Bluetooth setup failed (err %d)\n", err);
		return -4;
	}*/
	/*   From DevZone:
		 Calling `bt_le_adv_update_data()` consumed the remaining stack of the calling thread. Putting it in a workqueue gave it a separate stack, and that solved the issue. Increasing the stack size of the calling thread also solved it. 
	*/
	bt_register();	
	bt_submit_start_advertising_work();



	return 0;

}
