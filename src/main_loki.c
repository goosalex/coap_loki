#include "main_loki.h"
#include "motors/motor.h"

#include <zephyr/logging/log.h>

 uint16_t pwm_base;
 uint16_t pwm_period;
 uint16_t pwm_pulse;
 uint8_t speed_value;
 int8_t accel_order;
 uint8_t direction_pattern;

LOG_MODULE_DECLARE(logging_logic, LOG_LEVEL_DBG);

 void change_pwm_base(uint16_t new_base)
{
     pwm_base = new_base;
     pwm_period = NANO_PER_SECOND / pwm_base;
     pwm_pulse  = pwm_period / SPEED_STEPS;
	 LOG_DBG("PWM is %u Hz => %u .  %u / %u ns (1/%u)\n",  new_base, pwm_base, pwm_pulse, pwm_period, NANO_PER_SECOND);

     change_speed_directly(speed_value);
}


void notify_speed_change();

struct k_timer my_timer;
K_TIMER_DEFINE(my_timer, re_apply_acceleration	, NULL);

void apply_current_acceleration(){
	if ( (accel_order < 0) && -(accel_order) >= speed_value) {
		LOG_DBG("Breaking reached speed 0");
		speed_value = 0;
		accel_order = 0;
	}
	else if ((speed_value + accel_order) > SPEED_STEPS){
		LOG_DBG("Reached top speed %d", SPEED_STEPS);
		speed_value = SPEED_STEPS; // TODO: de-magify these values and put in Const
		accel_order = 0;
	}
	else{
		speed_value = accel_order + speed_value;
		LOG_DBG("New Speed 0x%02x\n",speed_value);
	}
	change_speed_directly(speed_value);
	notify_speed_change();
}

void re_apply_acceleration(struct k_timer *timer_id){
	LOG_DBG("Timer elapsed");
	apply_current_acceleration();
	if (speed_value == 0) k_timer_stop(&my_timer);
}

 void speed_set_acceleration(int8_t new_state)
{

	// TODO: update the accelaration rate
	// TODO: set a timer, regulary adding more speed according to power curve
	if (new_state <0 )
		LOG_DBG("Breaking 0x%02x per sec", new_state);
	else if (new_state == 0)
	{
		LOG_DBG("continueing coasting");
	}
	else
		LOG_DBG("Accelerating 0x%02x per sec", new_state);
	accel_order = new_state;
	// TODO: check if timer is running
	LOG_DBG("Timer state: 0x%04x\n",k_timer_remaining_get(&my_timer));
	if (k_timer_remaining_get(&my_timer) == 0){
		apply_current_acceleration();
		k_timer_start(&my_timer, K_SECONDS(1), K_SECONDS(1));
		LOG_DBG("Timer started");
	}

}

void change_speed_directly(uint8_t new_state){

	LOG_DBG("PWM is %u * %u / %u ns\n",  new_state, pwm_pulse, pwm_period);
    motor_speed_change_pwm(pwm_period , new_state * pwm_pulse);
	speed_value = new_state;
	LOG_DBG("Updated speed");
}

void change_direction(uint8_t new_pattern){
	if (direction_pattern != new_pattern) {
		if (speed_value > 0) {
			LOG_DBG("Still moving. Take order as emergency break in 3 seconds");
			// break within 3 seconds
			speed_set_acceleration(-1 * (speed_value / 3));
			return;
		}
		LOG_DBG ("changeing Direction from %u to %u", direction_pattern, new_pattern);
        motor_change_direction(new_pattern);
		direction_pattern = new_pattern;
	} else {
		LOG_DBG("Direction is already set to %u", direction_pattern);
	}
}

void define_light(){
	LOG_DBG("noop");
}

/* side: forward[1 bit], reverse[1 bit], 
   color: on: 0xFF......, off: 0x00...... , rgb: 0xA0RRGGBB 

*/
void set_lights(u_int8_t side, u_int32_t color, u_int8_t pattern){
	LOG_DBG("noop");
}

