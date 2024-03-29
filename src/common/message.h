/*
 * message.h
 *
 *  Created on: Sep 30, 2015
 *      Author: Abhijeet Wadkar, Devendra Dahiphale
 */

#ifndef MESSAGE_H_
#define MESSAGE_H_

#define MAX_TYPE_LENGTH 10
#define MAX_ACTION_LENGTH 50
#define CLOCK_SIZE 4

typedef enum message_type
{
	SWITCH,
	CURRENT_STATE,
	CURRENT_VALUE,
	SET_INTERVAL,
	REGISTER,
}message_type;

typedef enum device_type
{
	MOTION_SENSOR,
	DOOR_SENSOR,
	KEY_CHAIN_SENSOR,
	SECURITY_DEVICE,
	GATEWAY,
	BACK_TIER_GATEWAY,
	REPLICA_GATEWAY,
	FORWARD_GATEWAY,
	UNKNOWN
}device_type;

typedef struct message
{
	message_type type;
	long timestamp;
	int logical_clock[CLOCK_SIZE];
	struct
	{
		int value;
		struct
		{
			device_type type;
			char *ip_address;
			char *port_no;
			char *area_id;
		}s;
	}u;
}message;

#endif /* MESSAGE_H_ */
