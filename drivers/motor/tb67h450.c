#define DT_DRV_COMPAT toshiba_tb67h450

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <motor.h> 

LOG_MODULE_REGISTER(tb67h450, CONFIG_MOTOR_LOG_LEVEL);

struct tb67h450_config {
    struct pwm_dt_spec in1;
    struct pwm_dt_spec in2;
};

struct tb67h450_data {
    uint8_t direction; // 0=Stop, 1=Fwd, 2=Rev
};

static int tb67h450_set_direction(const struct device *dev, uint8_t direction) {
    struct tb67h450_data *data = dev->data;

    if (direction == 1 || direction == 2) {
        data->direction = direction;
    } else {
        data->direction = 0;
    }
    return 0;
}

static int tb67h450_set_speed(const struct device *dev, int speed) {
    const struct tb67h450_config *cfg = dev->config;
    struct tb67h450_data *data = dev->data;
    int err;
    uint32_t pulse_ns;

    // Clamp speed (0 to 100)
    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;

    // Calculate pulse
    if (speed > 0) {
        pulse_ns = (uint32_t)((uint64_t)cfg->in1.period * speed / 100);
    } else {
        pulse_ns = 0;
    }

    // Apply based on direction
    if (data->direction == 1) { // Forward: IN1=PWM, IN2=0
        pwm_set_pulse_dt(&cfg->in2, 0); 
        err = pwm_set_pulse_dt(&cfg->in1, pulse_ns);
    } else if (data->direction == 2) { // Reverse: IN1=0, IN2=PWM
        pwm_set_pulse_dt(&cfg->in1, 0);
        err = pwm_set_pulse_dt(&cfg->in2, pulse_ns);
    } else { // Stop
        pwm_set_pulse_dt(&cfg->in1, 0);
        err = pwm_set_pulse_dt(&cfg->in2, 0);
    }

    return err;
}

static const struct motor_driver_api tb67h450_api = {
    .set_speed = tb67h450_set_speed,
    .set_direction = tb67h450_set_direction,
};

static int tb67h450_init(const struct device *dev) {
    const struct tb67h450_config *cfg = dev->config;
    
    if (!pwm_is_ready_dt(&cfg->in1) || !pwm_is_ready_dt(&cfg->in2)) {
        LOG_ERR("PWM devices not ready");
        return -ENODEV;
    }
    
    // Init PWMs to 0
    pwm_set_pulse_dt(&cfg->in1, 0);
    pwm_set_pulse_dt(&cfg->in2, 0);

    return 0;
}

#define TB67H450_INIT(inst)                                        \
    static struct tb67h450_data data_##inst;                       \
    static const struct tb67h450_config config_##inst = {          \
        .in1 = PWM_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), 0),      \
        .in2 = PWM_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), 1),      \
    };                                                             \
    DEVICE_DT_INST_DEFINE(inst, tb67h450_init, NULL, &data_##inst, &config_##inst,  \
                          POST_KERNEL, CONFIG_MOTOR_INIT_PRIORITY, &tb67h450_api);

DT_INST_FOREACH_STATUS_OKAY(TB67H450_INIT)
