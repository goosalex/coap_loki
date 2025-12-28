/*
	Header file for motor driver files 
*/
#include <stdio.h>

#ifndef FUNC_H_
#define FUNC_H_

extern struct device *motor_gpio_dev;


extern int motor_init();


// Function to set speed directly
extern int motor_speed_change_pwm(uint32_t pwm_period, uint32_t pwm_pulse);

// set direction GPIO pins
extern void motor_change_direction(uint8_t new_pattern);


// Optional: Motor capabilities structure
struct motor_capabilities {
    uint8_t supports_pwm;
    uint8_t supports_direction;
    uint8_t max_channels;
};

extern const struct motor_capabilities* motor_get_capabilities(void);



#endif /* FUNC_H_ */