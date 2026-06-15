#ifndef MAIN_LOKI_H
#define MAIN_LOKI_H
#include <zephyr/kernel.h>
#include <stdint.h>

#include <openthread/message.h>
#include <openthread/nat64.h>
#include <openthread/udp.h>
#include <openthread/config.h>

#define NANO_PER_SECOND 1000000000
#define SPEED_STEPS 255

extern uint16_t pwm_base;
extern uint16_t pwm_period;
extern uint16_t pwm_pulse;
extern uint8_t speed_value;
extern int8_t accel_order;
extern uint16_t dcc_address;
extern uint8_t direction_pattern;
extern uint8_t speed_notify_enabled;

#define MAX_NAME_LENGTH 255



 void change_pwm_base(uint16_t new_base);
 void change_speed_directly(uint8_t new_state);
 void speed_set_acceleration(int8_t new_state);
void notify_motion_change();
int loki_motor_init(void);
void re_apply_acceleration(struct k_timer *timer_id);
void apply_current_acceleration();
void change_direction(uint8_t new_pattern);

/* Set the loco's DCC address, persist it to NVM, and re-register the SRP/Loconet
 * service so DNS-SD discovery and the UDP listener track the new value.
 * Safe to call from BLE writes or CoAP handlers. */
void apply_dcc_address(uint16_t new_dcc);

/* Build/refresh the SRP service registration for the current dcc_address and
 * rebind the Loconet UDP listener. Idempotent. Called from main() at boot
 * (after settings have been loaded) and internally from apply_dcc_address(). */
void register_dcc_service(void);

void on_udp_loconet_receive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);


#endif /* MAIN_LOKI_H */