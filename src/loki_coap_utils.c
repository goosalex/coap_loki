/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <openthread/ip6.h>
#include <openthread/message.h>
#include <openthread/thread.h>
#include <stdio.h>
#include "loki_coap_utils.h"
#include "loki_gatt.h"          /* MAX_LEN_FULL_NAME for name_request_handler */
#include "main_loki.h"
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>

#include "main_ot_utils.h"

LOG_MODULE_REGISTER(loki_coap_utils, CONFIG_OT_COAP_UTILS_LOG_LEVEL);

static otCoapOptionContentFormat getContentFormat(otMessage *request_message);

struct server_context {
	struct otInstance *ot;
	speed_request_callback_t on_speed_request;
	accel_request_callback_t on_acceleration_request;
	speed_request_callback_t on_direction_request;
	stop_request_t on_stop_request;
	name_set_request_callback_t on_name_request;
	ble_recovery_request_callback_t on_ble_recovery_request;
};

static struct server_context srv_context = {
	.ot = NULL,
	.on_speed_request = NULL,
	.on_acceleration_request = NULL,
	.on_direction_request = NULL,
	.on_stop_request = NULL,
	.on_name_request = NULL,
	.on_ble_recovery_request = NULL,
};


/**@brief Definition of CoAP resources for speed*/
static otCoapResource speed_resource = {
	.mUriPath = SPEED_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

/**@brief Definition of CoAP resources for acceleration*/
static otCoapResource acceleration_resource = {
	.mUriPath = ACC_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

/**@brief Definition of CoAP resources for direction*/
static otCoapResource direction_resource = {
	.mUriPath = DIRECTION_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

/**@brief Definition of CoAP resources for stop*/
static otCoapResource stop_resource = {
	.mUriPath = STOP_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};
/**@brief Definition of CoAP resources for name*/
static otCoapResource name_resource = {
	.mUriPath = NAME_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};
/**@brief Definition of CoAP resource for re-opening the BLE recovery window.
 * Any non-confirmable request reopens advertising for
 * CONFIG_LOKI_BLE_OFF_AFTER_ATTACH_MINUTES; ignored if that feature is off.
 */
static otCoapResource ble_recovery_resource = {
	.mUriPath = BLE_RECOVERY_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};
static otError provisioning_response_send(otMessage *request_message,
					  const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;

	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		goto end;
	}

	otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE,
			  OT_COAP_CODE_CONTENT);

	error = otCoapMessageSetToken(
		response, otCoapMessageGetToken(request_message),
		otCoapMessageGetTokenLength(request_message));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	payload = otThreadGetMeshLocalEid(srv_context.ot);
	payload_size = sizeof(otIp6Address);

	error = otMessageAppend(response, payload, payload_size);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendResponse(srv_context.ot, response, message_info);

	LOG_HEXDUMP_INF(payload, payload_size, "Sent provisioning response:");

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

	return error;
}

/* TODO REMOVE AFTER REIMPLEMENTATION FOR SPEED ACC STOP 
static void provisioning_request_handler(void *context, otMessage *message,
					 const otMessageInfo *message_info)
{
	otError error;
	otMessageInfo msg_info;

	ARG_UNUSED(context);

	if (!srv_context.provisioning_enabled) {
		LOG_WRN("Received provisioning request but provisioning "
			"is disabled");
		return;
	}

	LOG_INF("Received provisioning request");

	if ((otCoapMessageGetType(message) == OT_COAP_TYPE_NON_CONFIRMABLE) &&
	    (otCoapMessageGetCode(message) == OT_COAP_CODE_GET)) {
		msg_info = *message_info;
		memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

		error = (message, &msg_info);
		if (error == OT_ERROR_NONE) {
			srv_context.on_provisioning_request();
			srv_context.provisioning_enabled = false;
		}
	}
}

static void light_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	uint8_t command;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(message) != OT_COAP_TYPE_NON_CONFIRMABLE) {
		LOG_ERR("Light handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Light handler - Unexpected CoAP code");
		goto end;
	}

	if (otMessageRead(message, otMessageGetOffset(message), &command, 1) !=
	    1) {
		LOG_ERR("Light handler - Missing light command");
		goto end;
	}

	LOG_INF("Received light request: %c", command);

	srv_context.on_light_request(command);

end:
	return;
}
*/

static void speed_request_handler(void *context, otMessage *request_message,
								  const otMessageInfo *message_info)
{
	static char speed_input_value[4];
	uint8_t value;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(request_message) != OT_COAP_TYPE_NON_CONFIRMABLE)
	{
		LOG_ERR("Speed handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(request_message) == OT_COAP_CODE_PUT)
	{
		if (otMessageRead(request_message, otMessageGetOffset(request_message), &speed_input_value, 4) <
			1)
		{
			LOG_ERR("Speed handler - Missing speed parameter");
			goto end;
		}
		/* FIXME: enabled NEWLIBC only for this. Through out, if image grows too big:
		  FLASH:      692124 B         1 MB     66.01% 

		vs

           FLASH:      656124 B         1 MB     62.57%
             RAM:      131020 B       256 KB     49.98%
		*/
		sscanf(speed_input_value, "%d", &value);
		
		LOG_INF("Received direct speed request: %d", value);		
		srv_context.on_speed_request(value);
	}
	else if (otCoapMessageGetCode(request_message) == OT_COAP_CODE_GET)
	{
		/* Response code is fairly complicated. This example from provisioning example:
		 // Better put it in a generic method. And design Coap Interface well */
		otError error = OT_ERROR_NO_BUFS;
		otMessage *response;
		static char payload[40];
		uint16_t payload_size;

		response = otCoapNewMessage(srv_context.ot, NULL);
		if (response == NULL)
		{
			goto end_response;
		}

		otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE,
						  OT_COAP_CODE_CONTENT);

		error = otCoapMessageSetToken(
			response, otCoapMessageGetToken(request_message),
			otCoapMessageGetTokenLength(request_message));
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}
		if (getContentFormat(request_message) == OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN) {
		
			error = otCoapMessageAppendContentFormatOption(response, OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN);
			if (error != OT_ERROR_NONE)
			{
				goto end_response;
			}

			payload_size = sprintf(payload, "%d",speed_value);
		} else {
			error = otCoapMessageAppendContentFormatOption(response, OT_COAP_OPTION_CONTENT_FORMAT_OCTET_STREAM);
			if (error != OT_ERROR_NONE)
			{
				goto end_response;
			}
			payload_size = sizeof(speed_value);
		}		
		error = otCoapMessageSetPayloadMarker(response);
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}
		error = otMessageAppend(response, &payload, payload_size);
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}

		error = otCoapSendResponse(srv_context.ot, response, message_info);
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}
		LOG_INF("Sent direct speed response: %d", speed_value);

	end_response:
		if (error != OT_ERROR_NONE && response != NULL)
		{
			otMessageFree(response);
			LOG_ERR("Failed to send direct speed response");
		}

		goto end;
	}
	else
	{

		LOG_ERR("Speed handler - Unexpected CoAP code");
		goto end;
	}

end:
	return;
}

static otCoapOptionContentFormat getContentFormat(otMessage *request_message)
{
	/* Default to text/plain when the option is absent, matches what most
	 * CoAP clients assume when they don't set Content-Format. */
	otCoapOptionContentFormat content_format =
		OT_COAP_OPTION_CONTENT_FORMAT_TEXT_PLAIN;

	/* Iterator lives on the stack — the previous version dereferenced an
	 * uninitialised pointer. */
	otCoapOptionIterator iterator;
	const otCoapOption *option;
	otError error;

	error = otCoapOptionIteratorInit(&iterator, request_message);
	if (error != OT_ERROR_NONE) {
		return content_format;
	}

	option = otCoapOptionIteratorGetFirstOptionMatching(
		&iterator, OT_COAP_OPTION_CONTENT_FORMAT);
	if (option == NULL) {
		return content_format;
	}

	/* Content-Format is encoded as a CoAP uint option. */
	uint64_t raw = 0;
	error = otCoapOptionIteratorGetOptionUintValue(&iterator, &raw);
	if (error != OT_ERROR_NONE) {
		LOG_WRN("Failed to read content-format option: %d", error);
		return content_format;
	}
	return (otCoapOptionContentFormat)raw;
}

static void acceleration_request_handler(void *context, otMessage *request_message,
				  const otMessageInfo *message_info)
{
	int8_t value;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(request_message) != OT_COAP_TYPE_NON_CONFIRMABLE) {
		LOG_ERR("Acceleration handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(request_message) == OT_COAP_CODE_GET) {

		/* Response code is fairly complicated. This example from provisioning example:
		 // Better put it in a generic method. And design Coap Interface well */
		otError error = OT_ERROR_NO_BUFS;
		otMessage *response;
		const void *payload;
		uint16_t payload_size;

		response = otCoapNewMessage(srv_context.ot, NULL);
		if (response == NULL)
		{
			goto end_response;
		}

		otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE,
						  OT_COAP_CODE_CONTENT);

		error = otCoapMessageSetToken(
			response, otCoapMessageGetToken(request_message),
			otCoapMessageGetTokenLength(request_message));
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}

		error = otCoapMessageSetPayloadMarker(response);
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}

		/* GET /acceleration returns the current acceleration order. Until
		 * 2.6 lands a generic numeric handler, this branch is the only
		 * place that read accel_order; the previous `payload = speed_value`
		 * (an int assigned to a `const void *`) silently returned the
		 * speed instead. */
		payload = &accel_order;
		payload_size = sizeof(accel_order);

		error = otMessageAppend(response, payload, payload_size);
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}

		error = otCoapSendResponse(srv_context.ot, response, message_info);
		if (error != OT_ERROR_NONE)
		{
			goto end_response;
		}
		LOG_INF("Sent acceleration response: %d", accel_order);

	end_response:
		if (error != OT_ERROR_NONE && response != NULL)
		{
			otMessageFree(response);
			LOG_ERR("Failed to send acceleration response");
		}

		goto end;
	} else if (otCoapMessageGetCode(request_message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Acceleration handler - Unexpected CoAP code");
		goto end;
	} 

	if (otMessageRead(request_message, otMessageGetOffset(request_message), &value, 1) !=
	    1) {
		LOG_ERR("Acceleration handler - Missing accel/deccel parameter");
		goto end;
	}

	LOG_INF("Received acceleration/decceleration request: %c", value);

	srv_context.on_acceleration_request(value);

end:
	return;
}

static void direction_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	uint8_t value;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(message) != OT_COAP_TYPE_NON_CONFIRMABLE) {
		LOG_ERR("Direction handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Direction handler - Unexpected CoAP code");
		goto end;
	}

	if (otMessageRead(message, otMessageGetOffset(message), &value, 1) !=
	    1) {
		LOG_ERR("Direction handler - Missing direction parameter");
		goto end;
	}

	LOG_INF("Received direction request: %c", value);

	srv_context.on_direction_request(value);

end:
	return;
}

static void stop_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	ARG_UNUSED(message);
	ARG_UNUSED(message_info);

	LOG_INF("Received stop request");

	srv_context.on_stop_request();
}

static void name_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	ARG_UNUSED(message_info);

	LOG_INF("Received name request");

	if (srv_context.on_name_request == NULL) {
		return;
	}

	/* otMessageGetLength returns the total message length including the CoAP
	 * header; the payload starts at otMessageGetOffset(). */
	uint16_t offset = otMessageGetOffset(message);
	uint16_t total  = otMessageGetLength(message);
	uint16_t avail  = (total > offset) ? (total - offset) : 0;

	if (avail == 0) {
		LOG_WRN("Name request payload empty");
		return;
	}
	if (avail > MAX_LEN_FULL_NAME) {
		LOG_WRN("Name payload %u clamped to MAX_LEN_FULL_NAME (%u)",
			avail, MAX_LEN_FULL_NAME);
		avail = MAX_LEN_FULL_NAME;
	}

	/* Stack-allocate; the callback (modify_full_name) copies into the
	 * persistent full_name array before returning. No heap, no leak path. */
	char buf[MAX_LEN_FULL_NAME + 1];
	uint16_t got = otMessageRead(message, offset, buf, avail);
	if (got == 0) {
		LOG_ERR("Name request read failed");
		return;
	}
	buf[got] = '\0';

	srv_context.on_name_request(buf, got);
}

static void ble_recovery_request_handler(void *context, otMessage *message,
					  const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	ARG_UNUSED(message);
	ARG_UNUSED(message_info);

	LOG_INF("Received BLE recovery request");

	if (srv_context.on_ble_recovery_request) {
		srv_context.on_ble_recovery_request();
	}
}

static void coap_default_handler(void *context, otMessage *message,
				 const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	ARG_UNUSED(message);
	ARG_UNUSED(message_info);

	LOG_INF("Received CoAP message that does not match any request "
		"or resource");
}




/* TODO REMOVE after Re-implementation for speed, acc , stop
void ot_coap_activate_provisioning(void)
{
	srv_context.provisioning_enabled = true;
}

void ot_coap_deactivate_provisioning(void)
{
	srv_context.provisioning_enabled = false;
}

bool ot_coap_is_provisioning_active(void)
{
	return srv_context.provisioning_enabled;
}
*/
int loki_coap_init(
		speed_request_callback_t on_speed_request,
		accel_request_callback_t on_acceleration_request,
		speed_request_callback_t on_direction_request,
		stop_request_t on_stop_request,
		name_set_request_callback_t on_name_request,
		ble_recovery_request_callback_t on_ble_recovery_request)
	{
	otError error;

/* TODO REMOVE after Re-implementation for speed, acc , stop
	srv_context.provisioning_enabled = false;
	srv_context.on_provisioning_request = on_provisioning_request;
	srv_context.on_light_request = on_light_request;
*/
	srv_context.ot = openthread_get_default_instance();
	if (!srv_context.ot) {
		LOG_ERR("There is no valid OpenThread instance");
		printk("There is no valid OpenThread instance\n");			
		error = OT_ERROR_FAILED;
		goto end;
	}

	speed_resource.mContext = srv_context.ot;
	speed_resource.mHandler = speed_request_handler;
	acceleration_resource.mContext = srv_context.ot;
	acceleration_resource.mHandler = acceleration_request_handler;
	direction_resource.mContext = srv_context.ot;
	direction_resource.mHandler = direction_request_handler;
	stop_resource.mContext = srv_context.ot;
	stop_resource.mHandler = stop_request_handler;
	name_resource.mContext = srv_context.ot;
	name_resource.mHandler = name_request_handler;
	ble_recovery_resource.mContext = srv_context.ot;
	ble_recovery_resource.mHandler = ble_recovery_request_handler;

	otCoapSetDefaultHandler(srv_context.ot, coap_default_handler, NULL);
	otCoapAddResource(srv_context.ot, &speed_resource);
	otCoapAddResource(srv_context.ot, &acceleration_resource);
	otCoapAddResource(srv_context.ot, &direction_resource);
	otCoapAddResource(srv_context.ot, &stop_resource);
	otCoapAddResource(srv_context.ot, &name_resource);
	otCoapAddResource(srv_context.ot, &ble_recovery_resource);

	srv_context.on_speed_request = on_speed_request;
	srv_context.on_acceleration_request = on_acceleration_request;
	srv_context.on_direction_request = on_direction_request;
	srv_context.on_stop_request = on_stop_request;
	srv_context.on_name_request = on_name_request;
	srv_context.on_ble_recovery_request = on_ble_recovery_request;

	error = otCoapStart(srv_context.ot, COAP_PORT);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to start OT CoAP. Error: %d", error);
		printk("Failed to start OT CoAP. Error: %d", error);
		goto end;
	}
	printk("Coap Init done");
end:
	return error == OT_ERROR_NONE ? 0 : 1;
}


