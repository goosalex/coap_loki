/*
 * Copyright (c) 2024
 *
 * Generic Motor Driver API (Zephyr Driver Model)
 */

#ifndef SRC_MOTORS_MOTOR_H_
#define SRC_MOTORS_MOTOR_H_

#include <zephyr/device.h>
#include <zephyr/sys/errno_private.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Define the generic function signature */
typedef int (*motor_set_speed_t)(const struct device *dev, int speed_percent);
typedef int (*motor_set_direction_t)(const struct device *dev, uint8_t direction);

/* The API structure (vtable) */
struct motor_driver_api {
    motor_set_speed_t set_speed;
    motor_set_direction_t set_direction;
};

/* The Public Interface function */
static inline int motor_set_speed(const struct device *dev, int speed) {
    if (!dev) return -ENODEV;
    const struct motor_driver_api *api = 
        (const struct motor_driver_api *)dev->api;
    return api->set_speed(dev, speed);
}

static inline int motor_set_direction(const struct device *dev, uint8_t direction) {
    if (!dev) return -ENODEV;
    const struct motor_driver_api *api = 
        (const struct motor_driver_api *)dev->api;
    if (!api->set_direction) return -ENOTSUP;
    return api->set_direction(dev, direction);
}

#ifdef __cplusplus
}
#endif

#endif /* SRC_MOTORS_MOTOR_H_ */
