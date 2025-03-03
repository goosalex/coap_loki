#include "main_loki.h"
#include "motors/motor.h"

#include <zephyr/logging/log.h>
#include <openthread/udp.h>

 uint16_t pwm_base;
 uint16_t pwm_period;
 uint16_t pwm_pulse;
 uint8_t speed_value;
 int8_t accel_order;
 uint8_t direction_pattern;
 uint16_t dcc_address;

LOG_MODULE_REGISTER(logging_logic, LOG_LEVEL_DBG);

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

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  ((byte) & 0x80 ? '1' : '0'), \
  ((byte) & 0x40 ? '1' : '0'), \
  ((byte) & 0x20 ? '1' : '0'), \
  ((byte) & 0x10 ? '1' : '0'), \
  ((byte) & 0x08 ? '1' : '0'), \
  ((byte) & 0x04 ? '1' : '0'), \
  ((byte) & 0x02 ? '1' : '0'), \
  ((byte) & 0x01 ? '1' : '0') 


/*
* Reads one Loconet per UDP message and processes it if it is of interest
* means: for the correct DCC number and OPC_WR_SL_DATA
* see: https://wiki.rocrail.net/doku.php?id=loconet:ln-pe-en
*/
/**
 * Represents the local and peer IPv6 socket addresses.
 *
	typedef struct otMessageInfo
	{
		otIp6Address mSockAddr; ///< The local IPv6 address.
		otIp6Address mPeerAddr; ///< The peer IPv6 address.
		uint16_t     mSockPort; ///< The local transport-layer port.
		uint16_t     mPeerPort; ///< The peer transport-layer port.
		const void  *mLinkInfo; ///< A pointer to link-specific information.
		uint8_t      mHopLimit; ///< The IPv6 Hop Limit value. Only applies if `mAllowZeroHopLimit` is FALSE.
								///< If `0`, IPv6 Hop Limit is default value `OPENTHREAD_CONFIG_IP6_HOP_LIMIT_DEFAULT`.
								///< Otherwise, specifies the IPv6 Hop Limit.
		uint8_t mEcn : 2;       ///< The ECN status of the packet, represented as in the IPv6 header.
		bool    mIsHostInterface : 1;   ///< TRUE if packets sent/received via host interface, FALSE otherwise.
		bool    mAllowZeroHopLimit : 1; ///< TRUE to allow IPv6 Hop Limit 0 in `mHopLimit`, FALSE otherwise.
		bool    mMulticastLoop : 1;     ///< TRUE to allow looping back multicast, FALSE otherwise.
	} otMessageInfo;
*/

/*
	Loconet
					0							1		2		3       4       5       6      7        8       9       A       B       C       D			
	Symbol			Code	Description			Count	Arg1	Arg2	Arg3	Arg4	Arg5	Arg6	Arg7	Arg8	Arg9	Arg10	Arg11	ChkSum  Response
	OPC_WR_SL_DATA	0xEF	Write slot data.	0x0E	SLOT#	STAT1	ADR		SPD		DIRF	TRK		SS2		ADR2	SND		ID1		ID2				OPC_LONG_ACK
*/

void on_udp_loconet_receive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo){

    char buf[1500];
    int  length;
	char string[OT_IP6_ADDRESS_STRING_SIZE];
	const int OPC_WR_SL_DATA = 0xEF;
	u_int8_t ADR,ADR2;
	u_int16_t ADR16;

	otIp6AddressToString(&aMessageInfo->mPeerAddr, string, sizeof(string));
    LOG_INF("%d bytes from %s", otMessageGetLength(aMessage) - otMessageGetOffset(aMessage), string);

    length      = otMessageRead(aMessage, otMessageGetOffset(aMessage), buf, sizeof(buf) - 1);
    buf[length] = '\0';
	if (length == 0x0E && buf[1] == 0x0E && buf[0] == OPC_WR_SL_DATA){
		ADR = buf[4];
		ADR2 = buf[9];
		ADR16 = (ADR2 << 7) | ADR;
		LOG_INF("Received Loconet message: OPC_WR_SL_DATA for DCC %d", ADR16);
		if (ADR16 != dcc_address){
			LOG_INF("Not for me. I am DCC %d", dcc_address);
			return;	
		}
		uint8_t SPD, DIRF, STAT1;
		STAT1 = buf[3];
		LOG_INF("STAT1 Byte "BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(STAT1));

		/** Status byte 1
		 * D7-SL_SPURGE
			1=SLOT purge en,ALSO adrSEL (INTERNAL use only, not seen on NET!)
			CONDN/CONUP: bit encoding-Control double linked Consist List


			2 BITS for Consist
			D6-SL_CONUP
			D3-SL_CONDN
			11=LOGICAL MID CONSIST , Linked up AND down
			10=LOGICAL CONSIST TOP, Only linked downwards
			01=LOGICAL CONSIST SUB-MEMBER, Only linked upwards
			00=FREE locomotive, no CONSIST indirection/linking
			ALLOWS "CONSISTS of CONSISTS". Uplinked means that Slot SPD number is now SLOT adr of SPD/DIR and STATUS of consist. i.e. is an Indirect pointer. This Slot has same BUSY/ACTIVE bits as TOP of Consist. TOP is loco with SPD/DIR for whole consist. (top of list). BUSY/ACTIVE: bit encoding for SLOT activity


			2 BITS for BUSY/ACTIVE
			D5-SL_BUSY
			D4-SL_ACTIVE
			11=IN_USE loco adr in SLOT -REFRESHED
			10=IDLE loco adr in SLOT, not refreshed
			01=COMMON loco adr IN SLOT, refreshed
			00=FREE SLOT, no valid DATA, not refreshed


			3 BITS for Decoder TYPE encoding for this SLOT
			D2-SL_SPDEX
			D1-SL_SPD14
			D0-SL_SPD28
			010=  14 step MODE
			001=  28 step. Generate Trinary packets for this Mobile ADR
			000=  28 step/ 3 BYTE PKT regular mode
			011= 128 speed mode packets
			111= 128 Step decoder, Allow Advanced DCC consisting
			100=  28 Step decoder ,Allow Advanced DCC consisting
		*/
		DIRF = buf[6];
		LOG_INF("DIRF Byte "BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(DIRF));

		}


	LOG_DBG("noop");
}
