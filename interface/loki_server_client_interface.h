/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __LOKI_SERVER_CLIENT_INTRFACE_H__
#define __LOKI_SERVER_CLIENT_INTRFACE_H__

#define COAP_PORT 5683

/**@brief Enumeration describing directions commands. */
enum direction_command {
	THREAD_LOKI_UTILS_LIGHT_CMD_STOP = '0',
	THREAD_LOKI_UTILS_LIGHT_CMD_FORWARD = '1',
	THREAD_LOKI_UTILS_LIGHT_CMD_REVERSE = '2'
};

#define SPEED_URI_PATH "speed"
#define ACC_URI_PATH "acceleration"
#define DIRECTION_URI_PATH "direction"
#define STOP_URI_PATH "stop"

#endif
