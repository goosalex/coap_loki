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
#include <stdlib.h>
#include <inttypes.h>

#include <zephyr/net/openthread.h>
#include <openthread/thread.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>


#include "main_ble_utils.h"
#include "loki_coap_utils.h"
#include "main_ot_utils.h"
#include "main_loki.h"

#include "displays/main_display.h"

#include "motors/motor.h"
// BEGIN Settings and conditional NVM initialization related imports
#ifdef CONFIG_NVS
#include <zephyr/fs/nvs.h>
#elif defined(CONFIG_ZMS)
#include <zephyr/fs/zms.h>
#else
#error "Either CONFIG_NVS or CONFIG_ZMS must be enabled"
#endif
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/settings/settings.h>

#include "app_version.h"
// END Settings and NVM related imports

#if defined(CONFIG_LVGL) && defined(CONFIG_DISPLAY)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>
// Start Display related includes
#ifdef CONFIG_SSD1306
	//#include "displays/1306_display.c"
#endif


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




LOG_MODULE_REGISTER(loki_main, CONFIG_COAP_SERVER_LOG_LEVEL);

// BEGIN Settings and conditional NVM initialization related code. To be moved to separate file when stable
#define NVS_KEY_VERSION   0x10
#define NVS_KEY_BUILD     0x11

struct app_version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

static bool semver_major_minor_changed(const struct app_version *a,
                                       const struct app_version *b)
{
    return (a->major != b->major) || (a->minor != b->minor);
}

#ifdef CONFIG_NVS
static struct nvs_fs nvs;
#endif
// END SEttings and NVM

uint8_t speed_notify_enabled = 0;
bool is_display_enabled = false;

void notify_motion_change()
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
	if (is_display_enabled) {
		char buffer[3];
		if (direction_pattern && 1) {
			sprintf(buffer, "%s", " >");
		} else if ((direction_pattern && 2))
		{
			sprintf(buffer, "%s", "< ");
		} else
		 {
			sprintf(buffer, "%s", "--");
		}
		LOG_DBG("Sending Speed Notifications %s %d to Display\n",buffer, speed_value);
		display_updateDirectionAndSpeed(direction_pattern, speed_value);
	}

}


void stop_motor()
{
	change_speed_directly(0);
}

#if defined(CONFIG_NVS) && DT_HAS_CHOSEN(zephyr_storage)
#define LOKI_HAS_NVS_STORAGE 1
#define STORAGE_NODE DT_CHOSEN(zephyr_storage)

BUILD_ASSERT(
	DT_REG_SIZE(STORAGE_NODE) >= 0x4000,
	"zephyr,storage partition too small"
);
#else
#define LOKI_HAS_NVS_STORAGE 0
#endif

/* ---- NVS ---- */

int init_and_optionally_clear_nvs(void)
{
#if !LOKI_HAS_NVS_STORAGE
	LOG_WRN("NVS init skipped: missing CONFIG_NVS or chosen zephyr,storage");
	return 0;
#else
    const struct flash_area *fa;
    int err;

    struct app_version current_ver = {
        .major = APP_VERSION_MAJOR,
        .minor = APP_VERSION_MINOR,
        .patch = APP_VERSION_PATCH,
    };

    struct app_version stored_ver = {0};
	uint64_t stored_build = 0;
	uint64_t current_build = APP_BUILD_NUMBER;

    bool clear = false;

    /* Open NVS flash area */
	err = flash_area_open(FIXED_PARTITION_ID(STORAGE_NODE), &fa);
    if (err) {
        return err;
    }

    /* Configure NVS filesystem */
    nvs.flash_device = fa->fa_dev;
    nvs.offset = fa->fa_off;
    nvs.sector_size = fa->fa_size / 4;
    nvs.sector_count = 4;

#if defined(CONFIG_NVS)
    err = nvs_mount(&nvs);
#elif defined(CONFIG_ZMS)
    err = zms_mount(&nvs);
#endif
    if (err) {
        flash_area_close(fa);
        return err;
    }

    /* Read stored values (absence is OK) */
#if defined(CONFIG_NVS)
    err = nvs_read(&nvs, NVS_KEY_VERSION,
                   &stored_ver, sizeof(stored_ver));
#elif defined(CONFIG_ZMS)
    err = zms_read(&nvs, NVS_KEY_VERSION,
                   &stored_ver, sizeof(stored_ver));
#endif
    if (err < 0) {
        stored_ver = (struct app_version){0};
    }

#if defined(CONFIG_NVS)
    err = nvs_read(&nvs, NVS_KEY_BUILD,
                   &stored_build, sizeof(stored_build));
#elif defined(CONFIG_ZMS)
    err = zms_read(&nvs, NVS_KEY_BUILD,
                   &stored_build, sizeof(stored_build));
#endif
    if (err < 0) {
        stored_build = 0;
    }

    /* Version-based wipe (SemVer MAJOR.MINOR only) */
    if (IS_ENABLED(CONFIG_CLEAR_SETTINGS_NEW_VERSION) &&
        semver_major_minor_changed(&stored_ver, &current_ver)) {

		 printk("Version change %u.%u -> %u.%u\n",
               stored_ver.major, stored_ver.minor,
               current_ver.major, current_ver.minor);
        clear = true;
    }

    /* Build-based wipe */
    if (IS_ENABLED(CONFIG_CLEAR_SETTINGS_NEW_BUILD) &&
        stored_build != current_build) {

		 printk("Build change %" PRIu64 " -> %" PRIu64 "\n",
               stored_build, current_build);
        clear = true;
    }

    /* Clear storage if required */
    if (clear) {
#if defined(CONFIG_NVS)
        err = nvs_clear(&nvs);
#elif defined(CONFIG_ZMS)
        err = zms_clear(&nvs);
#endif
        if (err) {
            flash_area_close(fa);
            return err;
        }
    }

    /* Always store current version & build */
#if defined(CONFIG_NVS)
    nvs_write(&nvs, NVS_KEY_VERSION,
              &current_ver, sizeof(current_ver));
    nvs_write(&nvs, NVS_KEY_BUILD,
              &current_build, sizeof(current_build));
#elif defined(CONFIG_ZMS)
    zms_write(&nvs, NVS_KEY_VERSION,
              &current_ver, sizeof(current_ver));
    zms_write(&nvs, NVS_KEY_BUILD,
              &current_build, sizeof(current_build));
#endif

    flash_area_close(fa);
    return 0;
#endif
}

void init_default_values()
{
	speed_value = 0;
	accel_order = 0;
	direction_pattern = 0;
	speed_notify_enabled = 0;
	change_pwm_base(1000);

	dcc_address = 0;
}

void init_default_name()
{
	int id_len;
	int EUI64_LEN = 8;
	uint8_t eui64buf[EUI64_LEN];
	// certainly depends on CONFIG_HWINFO_NRF (or other if other device is chosen) in prj.conf
	id_len = hwinfo_get_device_id(eui64buf, EUI64_LEN);
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
	
}

void display_start(void)
{
	if (is_display_enabled) {
		char buffer[3];
		if (direction_pattern && 1) {
			sprintf(buffer, "%s", " >");
		} else if ((direction_pattern && 2))
		{
			sprintf(buffer, "%s", "< ");
		} else
		 {
			sprintf(buffer, "%s", "--");
		}
		display_updateDirectionAndSpeed(direction_pattern, speed_value);
		display_updateIPv6Address(NULL);
	}
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
		init_default_name();
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
		return 0;
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
	LOG_INF("Change of advertised short name will happen on next disconnect\n");
	return updateBleShortName(ble_name);
}




void init_display(void)
{
#if defined(CONFIG_LVGL) && defined(CONFIG_DISPLAY)
	is_display_enabled = true;
 	display_initDisplay();
	 display_start();
#endif
}



int main(void)
{
	int err;



	LOG_INF("%s","Startup Information:\n");
	if (loki_motor_init() != 0 ) {
		LOG_ERR("Motor init failed\n");
		return -1;
	}

	init_and_optionally_clear_nvs();
	init_default_values();


	init_display();
	display_updateConnectionStatus("Initializing...");
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth enable failed (err %d)\n", err);
		return -2;
	}
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		load_settings_from_nvm();
		err = settings_load();
			if (err) {
			LOG_WRN("Bluetooth and other settings load failed (err %d)\n", err);
			display_updateConnectionStatus("Settings load failed");
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
		display_updateOTConnectionStatus("+SRP");
		if (loki_coap_init(
			change_speed_directly,
			speed_set_acceleration,
			change_direction,
			stop_motor,
			modify_full_name,
			ble_lifecycle_force_recovery
			) != 0) {
				LOG_ERR("CoAP init failed\n");			
			} else {
				display_updateOTConnectionStatus("+S+CoAP");
				LOG_INF("CoAP initialized\n");
				/* short_name is already registered by init_srp() above —
				 * we only need to add the long-name service here. The
				 * boot path used to re-register short_name too, but that
				 * was a vestige of the per-TU duplicate bug (TODO/01 §1.6/§1.7). */
				if (long_name_coap_service != NULL) {
					LOG_INF("Service %s already registered as %s, freeing first", SRP_LONGNAME_SERVICE, full_name);
					otSrpClientBuffersFreeService(openthread_get_default_instance(), long_name_coap_service);
					long_name_coap_service = NULL;
				}
				long_name_coap_service = register_coap_service(
					openthread_get_default_instance(), full_name, SRP_LONGNAME_SERVICE);
				if (long_name_coap_service == NULL) {
					LOG_ERR("Failed to register long-name SRP service");
				}
			}
		/* Boot path: dcc_address was loaded from NVM by load_settings_from_nvm().
		 * The helper handles the "unset" (0) case and frees stale entries. */
		register_dcc_service();

	} else {
		LOG_INF("Thread not commissioned\n");
	}
	LOG_INF("Starting BLE\n");
	//settings_load_subtree("bt");

//	settings_load_subtree("bt");
//	settings_load_subtree("loki");
	updateBleShortName(ble_name);	
	updateBleLongName(full_name);

	display_updateName(ble_name);
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

	display_updateBTConnectionStatus("up");

	return 0;

}
