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

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/settings/settings.h>

#include <zephyr/drivers/pwm.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>
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




uint8_t speed_notify_enabled;




void notify_speed_change()
{
		// Notify if Notifications are enabled
	if (speed_notify_enabled) {
		printk("Sending Speed Notifications to GATT Client\n");
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

void set_name(char *buf, uint16_t len)
{
	
	// TODO: Implement, reset BLE Adv Name and restart BLE and CoAP
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
			ret = dk_set_led_on(OT_CONNECTION_LED);
			printk("OT new state Leader\n");
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
		ret = dk_set_led_off(OT_CONNECTION_LED);
			printk("OT Disabled\n");
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
		printk("Motor init failed\n");
		return -1;
	}

	if (loki_coap_init(
		change_speed_directly,
		speed_set_acceleration,
		change_direction,
		stop_motor,
		set_name
		) != 0) {
			printk("CoAP init failed\n");			
		} else {
				openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb);
			    openthread_start(openthread_get_default_context());
				printk("OT started\n");

		}


	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return -2;
	}
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
			if (err) {
			printk("Bluetooth settings load failed (err %d)\n", err);
			return -3;
		}
	}
	if (bt_ready() != 0) {
		printk("Bluetooth setup failed\n");
		return -4;
	}
	bt_register();	
	return 0;

}
