
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

#include "main_ble_utils.h"
#include "main_ot_utils.h"


LOG_MODULE_REGISTER(loki_ot, CONFIG_COAP_SERVER_LOG_LEVEL);

int enable_thread();

bool ot_is_enabled = false;
bool ot_is_commissioned = false;
bool srp_is_enabled = false;

void HandleJoinerCallback(otError aError, void *aContext) {
	if (aError == OT_ERROR_NONE) {
		LOG_INF("Joiner successfully joined the network");
		enable_thread();
		LOG_INF("Thread network enabled\n");
		init_srp();
		LOG_INF("SRP initialized\n");
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
	LOG_INF("Thread Network reset\n");
	LOG_INF("Ensure Interface is up\n");
	error = otIp6SetEnabled(p_instance, true);	
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to enable IP6: %s", otThreadErrorToString(error));
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
		// -11: Invalid State : (Get<ThreadNetif>().IsUp() && Get<Mle::Mle>().GetRole() == Mle::kRoleDisabled,
		return -1;
	}
	LOG_INF("Joiner started\n");
	return error;
}

static void on_thread_address_changed(otChangedFlags flags, struct openthread_context *ot_context,
				    void *user_data)
{
	if ( (flags & OT_CHANGED_IP6_ADDRESS_ADDED) || (flags & OT_CHANGED_IP6_ADDRESS_REMOVED) ) {
		LOG_INF("Thread IP Address changed\n");
		if (srp_is_enabled){
			init_srp();
		};
	}
	if ( (flags & OT_CHANGED_THREAD_ML_ADDR)  ) {
		LOG_INF("Thread ML Address changed\n");
		if (srp_is_enabled){
			init_srp();
		};
	}
		if ( (flags & OT_CHANGED_IP6_MULTICAST_SUBSCRIBED) || (flags & OT_CHANGED_IP6_MULTICAST_UNSUBSCRIBED) ) {
		LOG_INF("IPv6 Multicast subscriptions changed\n");
		if (srp_is_enabled){
			init_srp();
		};
	}
}

static void on_thread_state_changed(otChangedFlags flags, struct openthread_context *ot_context,
				    void *user_data)
{
	static int ret;
if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_CHILD:
			//ret = dk_set_led_on(OT_CONNECTION_LED);
			printk("OT new state  Childr\n");
			break;			
		case OT_DEVICE_ROLE_ROUTER:
			//ret = dk_set_led_on(OT_CONNECTION_LED);
			printk("OT new state Router\n");
			break;		
		case OT_DEVICE_ROLE_LEADER:
			//dk_set_led_on(OT_CONNECTION_LED);
			LOG_INF("Thread Role: Child/Router/Leader\n");
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			//dk_set_led_on(OT_CONNECTION_LED);
			LOG_INF("Thread Role: Disabled/Detached\n");
			// deactivate_provisionig();			
			break;
		}
	}
}
static struct openthread_state_changed_cb ot_state_chaged_cb = { .state_changed_cb =
									 on_thread_state_changed };
// why this Zephyr function is called and not otSetStateChangedCallback() ? IDK
static struct openthread_state_changed_cb ot_address_changed_cb = { .state_changed_cb =
									 on_thread_address_changed };



int enable_thread(){
	otInstance *p_instance = openthread_get_default_instance();
	if (ot_is_enabled) {
		LOG_INF("Thread network already enabled\n");
		return 0;
	}
	if (p_instance == NULL) {
		LOG_ERR("No OpenThread instance\n");
		return -1;
	}
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
	LOG_INF("Enabling Thread network\n");
	

	if (0 != openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb)){
		LOG_ERR("OpenThread State Change Callback Registration failed\n");
	};
	if (0 != openthread_state_changed_cb_register(openthread_get_default_context(), &ot_address_changed_cb)){
		LOG_ERR("OpenThread Address Change Callback Registration failed\n");
	};
	LOG_INF("OpenThread Callbacks registered\n");
	LOG_INF("Starting IPv6 network\n");
	error = otIp6SetEnabled(p_instance, true);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to enable IP6: %s", otThreadErrorToString(error));
		return -1;
	}
	LOG_INF("Starting Thread network\n");
	error = otThreadSetEnabled(p_instance, true);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to enable Thread (error: %d)", error);
		return -1;
	}	else {
		LOG_INF("OpenThread Started\n");
	}
	return 0;

}

	int get_Eui64(char *printable)
	{
		otInstance *p_instance = openthread_get_default_instance();
		otError error = OT_ERROR_NONE;
		otExtAddress eui64;
		otLinkGetFactoryAssignedIeeeEui64(p_instance, &eui64);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to get EUI64: %s", otThreadErrorToString(error));
			return -1;
		}
		sprintf(printable, "%02X%02X%02X%02X%02X%02X%02X%02X",
				eui64.m8[0], eui64.m8[1], eui64.m8[2], eui64.m8[3],
				eui64.m8[4], eui64.m8[5], eui64.m8[6], eui64.m8[7]);
		return 0;
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

otSrpClientAutoStartCallback aSrpClientAutoStartCallback(const otSockAddr *aServerSockAddr, void *aContext);

struct k_mutex srp_client_mutex;

void init_srp() {
	if (k_mutex_lock(&srp_client_mutex, K_MSEC(100)) == 0) {
    /* mutex successfully locked */


	otError error;
	otInstance *p_instance = openthread_get_default_instance();
	otSrpClientBuffersServiceEntry *entry;
	char *host_name;

	uint16_t size;

		if (otSrpClientIsRunning(p_instance)) {
			otSrpClientStop(p_instance);
			LOG_INF("Stopping SRP client...");
		} 

		LOG_INF("Initializing SRP client...");

		otSrpClientSetCallback(p_instance, srp_callback, NULL);
		// the host name is typically a 64byte string, but keep a look at &size (see openthread/src/core/srp_client/ot_srp_client.h)
		host_name = otSrpClientBuffersGetHostNameString(p_instance, &size);
		LOG_INF("Current host name :%s", host_name);
		// Copy first 9 bytes of ble short name into host name
		if (strcmp(ble_name, host_name) == 0) {
			LOG_INF("Host name already set to %s", ble_name);
			
		} else {
			if (strlen(ble_name) < size) {
				memcpy(host_name, ble_name, strlen(ble_name) + 1);
			} else {
				memcpy(host_name, ble_name, size - 1);
				host_name[size - 1] = '\0';
			}
			memcpy(host_name, ble_name, strlen(ble_name) + 1);
			error = otSrpClientSetHostName(p_instance, host_name);
			if (error != OT_ERROR_NONE) {
				LOG_ERR("Cannot set SRP client host name: %s",
						otThreadErrorToString(error));
				k_mutex_unlock(&srp_client_mutex);
				return;
			}
		}
		error = otSrpClientEnableAutoHostAddress(p_instance);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Cannot enable auto host address mode: %s",
					otThreadErrorToString(error));
			k_mutex_unlock(&srp_client_mutex);
			return;
		} else {
			LOG_INF("Auto host address mode enabled");
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
				//short_name_coap_service = entry->mService;
			}
		}
		LOG_INF("Attempt to enable auto start mode");
		otSrpClientEnableAutoStartMode(p_instance, aSrpClientAutoStartCallback, NULL);
		LOG_INF("SRP client initialized, waiting for callback");
		srp_is_enabled = true; // initial setup done
		k_mutex_unlock(&srp_client_mutex);

	} else {
		LOG_WRN("Cannot lock Init SRP client routines\n");

	}

	return;
}

otSrpClientAutoStartCallback aSrpClientAutoStartCallback(const otSockAddr *aServerSockAddr, void *aContext) {
	LOG_INF("SRP client auto start callback");
	if (aServerSockAddr != NULL) {
		char addr[OT_IP6_SOCK_ADDR_STRING_SIZE];
		otIp6AddressToString(&(aServerSockAddr->mAddress), addr, sizeof(addr));
		LOG_INF("SRP client started :%s %d", addr, aServerSockAddr->mPort);		
	} else {
		LOG_WRN("SRP client stopped");		
	}
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
