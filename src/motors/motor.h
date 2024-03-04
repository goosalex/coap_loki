/*
	Header file for motor driver files 
*/
#include <stdio.h>

#ifndef FUNC_H_
#define FUNC_H_

extern struct device *motor_gpio_dev;


extern void motor_init();


// Function to set speed directly
extern void motor_speed_change_pwm(uint32_t pwm_period, uint32_t pwm_pulse);

// set direction GPIO pins
extern void motor_change_direction(uint8_t new_pattern);

#endif /* FUNC_H_ */