
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


LOG_MODULE_DECLARE(loki_main, CONFIG_COAP_SERVER_LOG_LEVEL);

int enable_thread();

void HandleJoinerCallback(otError aError, void *aContext) {
	if (aError == OT_ERROR_NONE) {
		LOG_INF("Joiner successfully joined the network");
		enable_thread();
	} else {
		LOG_ERR("Joiner failed to join the network: %s", otThreadErrorToString(aError));
	}
}

int disable_thread(otInstance *p_instance){	
	otError error = OT_ERROR_NONE;
	otDeviceRole role = otThreadGetDeviceRole(p_instance);
    
    otOperationalDataset aDataset;
    otOperationalDatasetTlvs sDatasetTlvs;

	if (role == OT_DEVICE_ROLE_DISABLED) {
		LOG_INF("Thread network already disabled\n");
	} else {
	LOG_INF("Disabling Thread network\n");
		error = otThreadSetEnabled(p_instance, false);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to disable Thread (error: %d)", error);
			return -1;
		}
	}
    LOG_INF("Blanking out new Dataset var\n");

    memset(&aDataset, 0, sizeof(aDataset)); // clear out all data
	LOG_INF("Setting PanID\n");
    aDataset.mPanId = 0xFFFF; // broadcast PAN ID to find any network
    	LOG_INF("Set new Dataset Active\n");

    otDatasetConvertToTlvs(&aDataset, &sDatasetTlvs);
    error = otDatasetSetActiveTlvs(p_instance, &sDatasetTlvs);
   //  error = otDatasetSetActive(p_instance, &aDataset); // blank out the active dataset

    /*
	// override the current network settings with empty values
	error = otThreadSetNetworkName(p_instance, "");
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to set network name: %s", otThreadErrorToString(error));
		return -1;
	}

	error = otThreadSetExtendedPanId(p_instance, NULL);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to set extended PAN ID: %s", otThreadErrorToString(error));
		return -1;
	}
	error = otThreadSetNetworkKey(p_instance, NULL);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to set network key: %s", otThreadErrorToString(error));
		return -1;
	}

	otLinkSetPanId(p_instance, 0xffff); // broadcast PAN ID
    */
	return 0;
}

int start_thread_joiner(char *secret)
{

	otInstance *p_instance = openthread_get_default_instance();
	otError error = OT_ERROR_NONE;

	if (strlen(secret) < 6 ) {
		LOG_ERR("Secret too short\n");
		return -1;
	} else if (strlen(secret) > 32) {
		LOG_WRN("Secret too long\nAssuming active dataset\n");
		// TODO: Implement active dataset
		return -1;
	}
	LOG_INF("Starting Thread Network reset\n");
	// otInstanceFactoryReset(p_instance); // leads to complete reset, all non-volatile data is lost
	error = otThreadBecomeDetached(p_instance); // detaches from current network
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to detach from network: %s", otThreadErrorToString(error));

	}
	error = disable_thread(p_instance);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to disable current Thread settings: %s", otThreadErrorToString(error));
		return -1;
	}
    LOG_INF("Starting joiner\n");
	error = otJoinerStart(p_instance,
									secret,
									NULL,
									"OPENTHREAD", // Vendor, as of openthread CLI source code example
									"NONE", // Vendor Model, as of openthread CLI source code example
									"", // Vendor SW Version, as of openthread CLI source code example
									NULL,
									&HandleJoinerCallback,
									NULL
									);

	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to start joiner: %s", otThreadErrorToString(error));
		return -1;
	}
	LOG_INF("Joiner started\n");
	return error;
}

int enable_thread(){
	otInstance *p_instance = openthread_get_default_instance();
	otError error = OT_ERROR_NONE;
	otDeviceRole role = otThreadGetDeviceRole(p_instance);

	if (role == OT_DEVICE_ROLE_CHILD) {
		LOG_INF("Already connected to Thread network\n");
		return 0;
	}
	if (role == OT_DEVICE_ROLE_ROUTER) {
		LOG_INF("Already connected to Thread network\n");
		return 0;
	}
	if (role == OT_DEVICE_ROLE_LEADER) {
		LOG_INF("Already connected to Thread network\n");
		return 0;
	}
	LOG_INF("Joining Thread network\n");
	error = otThreadSetEnabled(p_instance, true);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to enable Thread (error: %d)", error);
		return -1;
	}

	return 0;
}