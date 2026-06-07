#define DT_DRV_COMPAT generic_dummy_motor

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <motor.h> /* Using src/motors/motor.h via include definitions */

LOG_MODULE_REGISTER(dummy_motor, CONFIG_MOTOR_LOG_LEVEL);

struct dummy_config {
    /* Define any config properties from your YAML binding */
};

struct dummy_data {
    /* Runtime state */
};

static int dummy_set_speed(const struct device *dev, int speed) {
    LOG_INF("Dummy Set Speed: %d", speed);
    return 0;
}

static int dummy_set_direction(const struct device *dev, uint8_t direction) {
    LOG_INF("Dummy Set Direction: %d", direction);
    return 0;
}

static const struct motor_driver_api dummy_api = {
    .set_speed = dummy_set_speed,
    .set_direction = dummy_set_direction,
};

static int dummy_init(const struct device *dev) {
    LOG_INF("Dummy Motor Driver Initialized");
    return 0;
}

#define DUMMY_INIT(inst)                                        \
    static const struct dummy_config dummy_config_##inst = { \
    };                                                                  \
    static struct dummy_data dummy_data_##inst = {};         \
    DEVICE_DT_INST_DEFINE(inst, dummy_init, NULL,   &dummy_data_##inst,                     \
                          &dummy_config_##inst,       \
                          POST_KERNEL, CONFIG_MOTOR_INIT_PRIORITY, &dummy_api);

DT_INST_FOREACH_STATUS_OKAY(DUMMY_INIT)
