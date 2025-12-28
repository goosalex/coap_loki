/*
    Dummy Motor Driver - No Operation Implementation
*/
#include "motor.h"
#include <zephyr/sys/printk.h>

static const struct motor_capabilities dummy_caps = {
    .supports_pwm = 0,
    .supports_direction = 0,
    .max_channels = 0
};

int motor_init(void)
{
    printk("Dummy motor driver initialized (no-op)\n");
    return 0;
}

int motor_speed_change_pwm(uint32_t pwm_period, uint32_t pwm_pulse)
{
    printk("Dummy motor: speed change ignored (period=%u, pulse=%u)\n", 
           pwm_period, pwm_pulse);
    return 0;
}

void motor_change_direction(uint8_t new_pattern)
{
    printk("Dummy motor: direction change ignored (pattern=%u)\n", new_pattern);
}

const struct motor_capabilities* motor_get_capabilities(void)
{
    return &dummy_caps;
}