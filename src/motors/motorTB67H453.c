/*
 * Copyright (c) 2024
 *
 * Motor driver for Toshiba TB67H453FNG in PWM (H-Bridge) mode.
 * 
 * Logic:
 * IN1 (Channel 1)   IN2 (Channel 2)   Operation
 * L                 L                 Stop (Standby)
 * H                 L                 Forward (CW)
 * L                 H                 Reverse (CCW)
 * H                 H                 Brake
 *
 * PWM Operation:
 * To control speed, we apply PWM to the active input pin while holding the other Low.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include "motor.h"

// Define the PWM node linkage
// We assume 'motor1pwm' alias/label points to the PWM controller channel 0 
// which shares the node/configuration with Channels 1 and 2.
#define PWM_NODE DT_NODELABEL(motor1pwm)

static const struct pwm_dt_spec pwm_dt = PWM_DT_SPEC_GET(PWM_NODE);

// Define channels based on board routing for H-Bridge
static const int IN1_CHANNEL = 1;
static const int IN2_CHANNEL = 2;

// Default Period (32768 Hz approx)
static const uint32_t DEF_PERIOD = 30518; 

// State
static int current_active_channel = 0; // 0 = None/Stop
static uint32_t current_period = 30518;
static uint32_t current_pulse = 0;

static const struct motor_capabilities caps = {
    .supports_pwm = 1,
    .supports_direction = 1,
    .max_channels = 2
};

const struct motor_capabilities* motor_get_capabilities(void)
{
    return &caps;
}

int motor_init() {
    if (!pwm_is_ready_dt(&pwm_dt)) {
        printk("Error: PWM device %s is not ready\n", pwm_dt.dev->name);
        return -1;
    }

    // Initialize both channels to OFF (Coast/Stop: L, L)
    // We use pwm_dt.flags to handle potential nrf invert logic
    if (pwm_set(pwm_dt.dev, IN1_CHANNEL, DEF_PERIOD, 0, pwm_dt.flags)) {
        printk("Error: Failed to init IN1 (Ch %d)\n", IN1_CHANNEL);
        return -1;
    }
    if (pwm_set(pwm_dt.dev, IN2_CHANNEL, DEF_PERIOD, 0, pwm_dt.flags)) {
        printk("Error: Failed to init IN2 (Ch %d)\n", IN2_CHANNEL);
        return -1;
    }

    current_active_channel = IN1_CHANNEL; // Default selection
    current_pulse = 0;
    
    printk("TB67H453 Motor Driver Initialized\n");
    return 0;
}

int motor_speed_change_pwm(uint32_t pwm_period, uint32_t pwm_pulse) {
    int ret = 0;
    
    current_period = pwm_period;
    current_pulse = pwm_pulse;

    if (current_active_channel != 0) {
        ret = pwm_set(pwm_dt.dev, current_active_channel, pwm_period, pwm_pulse, pwm_dt.flags);
        if (ret) {
            printk("Error: Failed to set speed on Ch %d\n", current_active_channel);
        }
    }
    // If channel is 0 (Stop), we just stored the values. Next direction change will apply them? 
    // Or should we enforce Stop?
    // In L/L state (channel=0), pwm_set was called with 0 in change_direction.
    
    return ret;
}

void motor_change_direction(uint8_t new_pattern) {
    int new_channel = 0;

    // Decode pattern
    // 1 = Forward (IN1), 2 = Reverse (IN2)
    // 0 = Stop, 3 = Brake (Not fully implemented, treated as Forward logic priority or ignored)
    if ((new_pattern & 0x01) == 1) {
        new_channel = IN1_CHANNEL;
    } else if ((new_pattern & 0x02) == 0x02) {
        new_channel = IN2_CHANNEL;
    } else {
        new_channel = 0; // Stop
    }

    if (new_channel == current_active_channel) {
        return; 
    }

    // 1. Disable current channel (Set to 0/Low)
    if (current_active_channel != 0) {
        pwm_set(pwm_dt.dev, current_active_channel, current_period, 0, pwm_dt.flags);
    }
    
    // 2. Enable new channel logic
    current_active_channel = new_channel;

    // 3. Apply current speed to new channel (if not Stop)
    if (current_active_channel != 0) {
        pwm_set(pwm_dt.dev, current_active_channel, current_period, current_pulse, pwm_dt.flags);
    }
}
