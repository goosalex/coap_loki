/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __OT_COAP_UTILS_H__
#define __OT_COAP_UTILS_H__

#include <loki_server_client_interface.h>

/**@brief Type definition of the function used to handle light resource change.
 */
typedef void (*speed_request_callback_t)(uint8_t cmd);
typedef void (*accel_request_callback_t)(int8_t cmd);
typedef void (*stop_request_t)();

int loki_coap_init(
		speed_request_callback_t on_speed_request,
		accel_request_callback_t on_acceleration_request,
		speed_request_callback_t on_direction_request,
		stop_request_t on_stop_request);		

#endif
