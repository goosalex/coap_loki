#ifndef MAIN_LOKI_H
#define MAIN_LOKI_H
#include <zephyr/kernel.h>
#include <stdint.h>

#define NANO_PER_SECOND 1000000000
#define SPEED_STEPS 255

extern uint16_t pwm_base;
extern uint16_t pwm_period;
extern uint16_t pwm_pulse;
extern uint8_t speed_value;
extern int8_t accel_order;
extern uint8_t direction_pattern;
extern uint8_t speed_notify_enabled;



 void change_pwm_base(uint16_t new_base);
 void change_speed_directly(uint8_t new_state);
 void speed_set_acceleration(int8_t new_state);
void notify_speed_change();
void re_apply_acceleration(struct k_timer *timer_id);
void apply_current_acceleration();
void change_direction(uint8_t new_pattern);

#endif /* MAIN_LOKI_H */