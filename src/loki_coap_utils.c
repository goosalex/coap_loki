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

#include "loki_coap_utils.h"

LOG_MODULE_REGISTER(loki_coap_utils, CONFIG_OT_COAP_UTILS_LOG_LEVEL);

struct server_context {
	struct otInstance *ot;
	speed_request_callback_t on_speed_request;
	accel_request_callback_t on_acceleration_request;
	speed_request_callback_t on_direction_request;
	stop_request_t on_stop_request;
	
};

static struct server_context srv_context = {
	.ot = NULL,
	.on_speed_request = NULL,
	.on_acceleration_request = NULL,
	.on_direction_request = NULL,
	.on_stop_request = NULL,
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

		error = provisioning_response_send(message, &msg_info);
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

static void speed_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	uint8_t value;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(message) != OT_COAP_TYPE_NON_CONFIRMABLE) {
		LOG_ERR("Speed handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Speed handler - Unexpected CoAP code");
		goto end;
	}

	if (otMessageRead(message, otMessageGetOffset(message), &value, 1) !=
	    1) {
		LOG_ERR("Speed handler - Missing speed parameter");
		goto end;
	}

	LOG_INF("Received direct speed request: %c", value);

	srv_context.on_speed_request(value);

end:
	return;
}

static void acceleration_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	int8_t value;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(message) != OT_COAP_TYPE_NON_CONFIRMABLE) {
		LOG_ERR("Acceleration handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Acceleration handler - Unexpected CoAP code");
		goto end;
	}

	if (otMessageRead(message, otMessageGetOffset(message), &value, 1) !=
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
		stop_request_t on_stop_request)
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

	otCoapSetDefaultHandler(srv_context.ot, coap_default_handler, NULL);
	otCoapAddResource(srv_context.ot, &speed_resource);
	otCoapAddResource(srv_context.ot, &acceleration_resource);
	otCoapAddResource(srv_context.ot, &direction_resource);
	otCoapAddResource(srv_context.ot, &stop_resource);

	error = otCoapStart(srv_context.ot, COAP_PORT);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to start OT CoAP. Error: %d", error);
		goto end;
	}

end:
	return error == OT_ERROR_NONE ? 0 : 1;
}


