#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include "motor.h"
/*
	Configure PWM for speed/motor output. (14 is LED2 on DK)
*/
#define PWM_NODE DT_NODELABEL(motor1pwm)

static struct pwm_dt_spec pwm_dt = PWM_DT_SPEC_GET(PWM_NODE);

/* TODO CHECK for something useful here
#if DT_NODE_HAS_STATUS(DT_ALIAS(motorpwm), okay)
// #define PWM_DRIVER  DT_PWMS_LABEL(DT_ALIAS(motorpwm))
#else
#error "Unsupported board: motorpwm devicetree alias is not defined or enabled"
#endif
*/

static int PWM_CHANNEL    = 0;
static const int DIR_A_CHANNEL  = 1;
static const int DIR_B_CHANNEL  = 2;
static int DEF_PERIOD = 30518; // in ns => 32768 Hz;

/* FIXME: NOt sure, this works at all 
static int fix_pwm_as_gpio(int channel, int value){
	int pulse = 0;
	pwm_flags_t flags = pwm_dt.flags;
	if (value > 0 ) { // HIGH = 0 in POLARITY_INVERTED
		flags  = flags | PWM_POLARITY_INVERTED;
	} else {
		flags  = flags & ~PWM_POLARITY_INVERTED;
	}
	// see https://docs.zephyrproject.org/latest/doxygen/html/group__pwm__interface.html#ga8ff263177143d33c6d0a284b837bc4da
	return pwm_set(pwm_dt.dev, channel, pwm_dt.period, pulse, flags);   
}
*/

 int motor_init(){
	PWM_CHANNEL = DIR_B_CHANNEL;
	// initally set both pins to 0 speed
	if (motor_speed_change_pwm(DEF_PERIOD, 0) != 0){
		printk("motor_init %d: pwm set fails\n", DIR_B_CHANNEL);
		return 1;
	}
	PWM_CHANNEL = DIR_A_CHANNEL;
	// initally set direction to A
	if (motor_speed_change_pwm(DEF_PERIOD, 0) != 0){
		printk("motor_init %d: pwm set fails\n", DIR_A_CHANNEL);
		return 1;
	}
	return 0;
}

// Function to set speed directly
 int motor_speed_change_pwm(uint32_t pwm_period, uint32_t pwm_pulse){
	if (PWM_CHANNEL == 0){
				printk("Direction not set\n");
		return 0;
	}
	if (pwm_set(pwm_dt.dev, PWM_CHANNEL, pwm_period , pwm_pulse, pwm_dt.flags))
	{
		printk("pwm pin set fails\n");
		return 1;
	}
	return 0;
}

// set direction GPIO pins
 void motor_change_direction(uint8_t new_pattern){

	if ((new_pattern & 0x01) == 1)
	{
		PWM_CHANNEL = DIR_A_CHANNEL;
	}
	else if ((new_pattern & 0x02) == 2)
	{
		PWM_CHANNEL = DIR_B_CHANNEL;
	}
	else
	{
		PWM_CHANNEL = 0;
	}
}

	

