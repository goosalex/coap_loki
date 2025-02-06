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

#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>

#include <dk_buttons_and_leds.h>
#include "main_ble_utils.h"
#include "loki_coap_utils.h"
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

// e.g. LOKI0815._ble._loki._coap._udp.local or BR212._ble._loki._coap._udp.local
static otSrpClientService short_name_coap_service;
#define SRP_SHORTNAME_SERVICE  "_ble._loki._coap._udp"
// e.g. "Keihan Otsu Line Type 700 [Sound! Euphonium] Wrapping Train 2023"._name._loki._coap._udp.local
static otSrpClientService long_name_coap_service;
#define SRP_LONGNAME_SERVICE  "_name._loki._coap._udp"
// e.g. 53._dcc._loki._coap._udp.local
static otSrpClientService dcc_name_coap_service;
#define SRP_DCC_SERVICE  "_dcc._loki._coap._udp"


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

	dcc_address = 3;
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
		printk("settings subsys initialization: fail (err %d)\n", rc);
		return;
	}

	printk("settings subsys initialization: OK.\n");

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
	
	newBleAdvName(ble_name);
	init_srp();
}

void srp_callback(otError error, const otSrpClientHostInfo *aHostInfo,
                  const otSrpClientService *aServices,
                  const otSrpClientService *aRemovedServices, void *aContext) {
  if (error != OT_ERROR_NONE) {
    LOG_ERR("SRP update error: %s", otThreadErrorToString(error));
    return;
  }

  LOG_INF("SRP update registered");
  return;
}



void init_srp() {
  otError error;
  otInstance *p_instance = openthread_get_default_instance();
  otSrpClientBuffersServiceEntry *entry;
  char *host_name;

  uint16_t size;

	if (otSrpClientIsRunning(p_instance)) {
		otSrpClientStop(p_instance);
		LOG_INF("Stopping SRP client...");
	} else {

		LOG_INF("Initializing SRP client...");

		otSrpClientSetCallback(p_instance, srp_callback, NULL);
		host_name = otSrpClientBuffersGetHostNameString(p_instance, &size);
		memcpy(host_name, ble_name, strlen(ble_name) + 1);
		error = otSrpClientSetHostName(p_instance, host_name);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Cannot set SRP client host name: %s",
					otThreadErrorToString(error));
			return;
		}

		error = otSrpClientEnableAutoHostAddress(p_instance);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Cannot enable auto host address mode: %s",
					otThreadErrorToString(error));
			return;
		}

	}
	if ( short_name_coap_service.mInstanceName != NULL
		&& strcmp(short_name_coap_service.mInstanceName, ble_name) == 0 )
	{
		LOG_INF("Service %s already registered as %s", SRP_SHORTNAME_SERVICE, ble_name);
	} else {
		entry = register_service(p_instance, ble_name, SRP_SHORTNAME_SERVICE);
		if (entry == NULL) {
			LOG_ERR("Cannot allocate new service entry under %s", SRP_SHORTNAME_SERVICE);
		} else {
			LOG_INF("Service %s registered as %s", SRP_SHORTNAME_SERVICE, ble_name);
			short_name_coap_service = entry->mService;
		}
	}
  

  otSrpClientEnableAutoStartMode(p_instance, NULL, NULL);

}

otSrpClientBuffersServiceEntry *register_service( otInstance *p_instance ,  char *instance_name, char *service_name){
	otError error;
	char *instance_name_buf;
    char *service_name_buf;
	otSrpClientBuffersServiceEntry *entry;
	uint16_t size;

	entry = otSrpClientBuffersAllocateService(p_instance);
	if (entry == NULL) {
		LOG_ERR("Cannot allocate new service entry");
		return NULL;
	}
	instance_name_buf =
		otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size);
	memcpy(instance_name_buf, instance_name, strlen(instance_name) + 1);
	service_name_buf =
		otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
	memcpy(service_name_buf, service_name, strlen(service_name) + 1);
	entry->mService.mPort = OT_DEFAULT_COAP_PORT;
	error = otSrpClientAddService(p_instance, &entry->mService);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Cannot add service: %s", otThreadErrorToString(error));
		return NULL;
	}

	return entry;

}


static void on_thread_state_changed(otChangedFlags flags, struct openthread_context *ot_context,
				    void *user_data)
{
	static int ret;
if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_CHILD:
			ret = dk_set_led_on(OT_CONNECTION_LED);
			printk("OT new state  Childr\n");
			break;			
		case OT_DEVICE_ROLE_ROUTER:
			ret = dk_set_led_on(OT_CONNECTION_LED);
			printk("OT new state Router\n");
			break;		
		case OT_DEVICE_ROLE_LEADER:
			dk_set_led_on(OT_CONNECTION_LED);
			LOG_INF("Thread Role: Child/Router/Leader\n");
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			dk_set_led_on(OT_CONNECTION_LED);
			LOG_INF("Thread Role: Disabled/Detached\n");
			// deactivate_provisionig();
			break;
		}
	}
}
static struct openthread_state_changed_cb ot_state_chaged_cb = { .state_changed_cb =
									 on_thread_state_changed };




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

	if (loki_coap_init(
		change_speed_directly,
		speed_set_acceleration,
		change_direction,
		stop_motor,
		modify_full_name
		) != 0) {
			LOG_ERR("CoAP init failed\n");			
		} else {
			if (0 != openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb)){
				LOG_ERR("OpenThread State Change Callback Registration failed\n");
			};
			if ( 0!= openthread_start(openthread_get_default_context())){
				LOG_ERR("OpenThread Start failed\n");
			} else {
				LOG_INF("OpenThread Started\n");
			}
		}


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
