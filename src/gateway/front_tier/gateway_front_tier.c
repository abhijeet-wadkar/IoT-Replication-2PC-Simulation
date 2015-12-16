/*
 * gateway.c
 *
 *  Created on: Oct 1, 2015
 *      Author: Abhijeet Wadkar, Devendra Dahiphale
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <queue.h>

#include "gateway.h"
#include "error_codes.h"
#include "logger.h"
#include "network_functions.h"
#include "network_read_thread.h"
#include "logical_clock_utils.h"
#include "string_helper_functions.h"

char* device_string[] = {
		"motion_sensor",
		"door_sensor",
		"key_chain_sensor",
		"security_device",
		"gateway",
		"back_tier_gateway",
		"unknown"};

char* message_string[] = {
		"switch",
		"current_state",
		"current_value",
		"set_interval",
		"register"
};

char* state_string[] = {
		"off",
		"on"
};

char *value_string[] = {
		"false",
		"true"
};

char *door_string[] = {
		"close",
		"open"
};

typedef struct message_context
{
	gateway_client *client;
	message *msg;
}message_context;

typedef struct transaction_context
{
	gateway_context *gateway;
	gateway_client *client;
	char *buffer;
}transaction_context;

void* accept_callback(void *context);
void* primary_gateway_callback(void *context);
void* read_callback(void*);
void* forwarding_read_callback(void*);
void* message_handler(void*);
void print_state(gateway_context *gateway);

int send_message_to_replicas(gateway_context *gateway, char *message);
int vote_from_replica(gateway_context *gateway, gateway_client *client);
int receive_ack_from_replica(gateway_context *gateway, gateway_client *client);
int two_phase_commit(gateway_context *gateway, gateway_client *client, char* commit_message);


static void security_system_switch(gateway_context *gateway, int value)
{
	message msg;
	msg.type = SWITCH;
	msg.u.value = value;
	for(int index = 0; index < gateway->client_count; index++)
	{
		if(gateway->clients[index]->type == SECURITY_DEVICE)
		{
			int return_value = write_message(gateway->clients[index]->comm_socket_fd, gateway->logical_clock, &msg);
			if (E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: unable to send the message\n"));
			}
			break;
		}
	}
}

void* start_transaction(void *context)
{
	int return_value;
	transaction_context *tc = (transaction_context*)context;
	int flag = 0;
	int back_end_sock_fd;
	char add_number_buffer[200] = {'\0'};

	for(int index=0; index<tc->gateway->client_count; index++)
	{
		if(tc->gateway->clients[index]->type == REPLICA_GATEWAY
				&& tc->gateway->clients[index]->comm_socket_fd != -1)
		{
			flag = 1;
		}
		if(tc->gateway->clients[index]->type == BACK_TIER_GATEWAY)
		{
			back_end_sock_fd = tc->gateway->clients[index]->comm_socket_fd;
		}
	}

	if(flag == 1)
	{
		pthread_mutex_lock(&tc->gateway->transaction_lock);
		while(tc->gateway->two_pc_state != NOTHING);

		return_value = two_phase_commit(tc->gateway, tc->client, tc->buffer);

		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: TwoPhaseCommit Failed\n"));
		}

		pthread_mutex_unlock(&tc->gateway->transaction_lock);
	}
	else
	{
		LOG_SCREEN(("INFO: No Replica Found: Committing to local database\n"));
		tc->gateway->transaction_number++;
		sprintf(add_number_buffer, "TID:%d----%s", tc->gateway->transaction_number, tc->buffer);
		return_value = send_msg_to_backend(back_end_sock_fd, add_number_buffer);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
		}
	}

	pthread_exit(NULL);

	return NULL;
}

void* message_handler(void *context)
{
	message_context *msg_context = NULL;
	gateway_client *client = NULL;
	gateway_context *gateway = (gateway_context*)context;
	message *msg = NULL;
	int sensor_status[3] = {0};
	int return_value;
	char buffer[100] = {'\0'};
	gateway->door_state = -1;
	gateway->key_state = -1;
	gateway->motion_state = -1;

	while(1)
	{
		pthread_mutex_lock(&gateway->mutex_lock);
		if(isEmpty(gateway->msg_queue))
		{
			pthread_cond_wait(&gateway->cond_lock, &gateway->mutex_lock);
		}

		/* Process message */
		msg_context = remove_queue(&gateway->msg_queue);
		client = msg_context->client;
		msg = msg_context->msg;

		switch(msg->type)
		{
		case SET_INTERVAL:
			LOG_DEBUG(("DEBUG: SetInterval message received, Value: %d\n", msg->u.value));
			break;
		case REGISTER:
			LOG_DEBUG(("DEBUG: Register message received\n"));
			client->type = msg->u.s.type;
			client->client_ip_address = msg->u.s.ip_address;
			client->client_port_number = msg->u.s.port_no;
			client->area_id = msg->u.s.area_id;
			LOG_DEBUG(("DEBUG: DeviceType:%d\n", client->type));
			LOG_DEBUG(("DEBUG: IP Address: %s\n", client->client_ip_address));
			LOG_DEBUG(("DEBUG: Port Number: %s\n", client->client_port_number));
			LOG_DEBUG(("DEBUG: Area Id: %s\n", client->area_id));
			if(client->type == DOOR_SENSOR ||
				client->type == MOTION_SENSOR ||
				client->type == KEY_CHAIN_SENSOR)
			{
				client->state = 1;
			}
			if(client->type == SECURITY_DEVICE)
			{
				client->state = 0;
			}

			// Check if all the components of the system are connected to the gateway
			if (gateway->client_count == 7 && gateway->primary_flag == 1)
			{
				char load_balancing_factor = '0';
				LOG_SCREEN(("All Devices registered successfully\n"));
				for(int index=0; index < gateway->client_count; index++)
				{
					if(gateway->clients[index]->type == MOTION_SENSOR
							|| gateway->clients[index]->type == KEY_CHAIN_SENSOR
							|| gateway->clients[index]->type == DOOR_SENSOR)
					{
						for(int index1=0; index1<gateway->client_count; index1++)
						{
							if(0 != strcmp(gateway->clients[index]->client_port_number, gateway->clients[index1]->client_port_number)
									&&
									(gateway->clients[index1]->type == MOTION_SENSOR
											|| gateway->clients[index1]->type == KEY_CHAIN_SENSOR
											|| gateway->clients[index1]->type == DOOR_SENSOR
											|| gateway->clients[index1]->type == REPLICA_GATEWAY))
							{
								message msg;
								msg.type = REGISTER;
								msg.u.s.type = gateway->clients[index1]->type;
								msg.u.s.ip_address = gateway->clients[index1]->client_ip_address;
								msg.u.s.port_no = gateway->clients[index1]->client_port_number;
								if(gateway->clients[index1]->type != REPLICA_GATEWAY)
									load_balancing_factor++;
								*(gateway->clients[index1]->area_id) = load_balancing_factor; 
								msg.u.s.area_id = gateway->clients[index1]->area_id;
								return_value = write_message(gateway->clients[index]->comm_socket_fd, gateway->logical_clock, &msg);
								if (E_SUCCESS != return_value)
								{
									LOG_ERROR(("ERROR: unable to send the message\n"));
								}
							}
						}
					}
				}
			}
			break;
		case CURRENT_VALUE:
			LOG_DEBUG(("Current value message received\n"));
			LOG_DEBUG(("Value: %d\n", msg->u.value));

			client->value = msg->u.value;

			if(client->type == MOTION_SENSOR)
			{
				sprintf(buffer, "1----%-20s----%-10s----%-10lu----%-10s----%-10s\n",
					device_string[client->type],
					value_string[msg->u.value],
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number);
				gateway->motion_state = msg->u.value;
				if(sensor_status[2] != 3)
				{
					for(int i = 0; i < 3; i++)
					{
						if(sensor_status[i] > 1)
						{
							sensor_status[i]--;
						}
						sensor_status[2] = 3;
					}
				}
			}
			else if(client->type == DOOR_SENSOR)
			{
				sprintf(buffer, "2----%-20s----%-10s----%-10lu----%-10s----%-10s\n",
					device_string[client->type],
					door_string[msg->u.value],
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number);
				gateway->door_state = msg->u.value;

				if(sensor_status[0] != 3)
				{
					for(int i = 0; i < 3; i++)
					{
						if(sensor_status[i] > 1)
						{
							sensor_status[i]--;
						}
						sensor_status[0] = 3;
					}
				}
			}
			else if(client->type == KEY_CHAIN_SENSOR)
			{
				sprintf(buffer, "3----%-20s----%-10s----%-10lu----%-10s----%-10s\n",
					device_string[client->type],
					value_string[msg->u.value],
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number);
				gateway->key_state = msg->u.value;
				if(sensor_status[1] != 3)
				{
					for(int i = 0; i < 3; i++)
					{
						if(sensor_status[i] > 1)
						{
							sensor_status[i]--;
						}
						sensor_status[1] = 3;
					}
				}
			}
			//print_state(client->gateway);


			/*for(int index=0; index<gateway->client_count; index++)
			{
				if(gateway->clients[index]->type == BACK_TIER_GATEWAY)
				{
					return_value = send_msg_to_backend(gateway->clients[index]->comm_socket_fd, buffer);
					if(E_SUCCESS != return_value)
					{
						LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
					}
				}
			}*/

			/* starting the 2Phase commit protocol */
			pthread_t thread;
			transaction_context *tc = (transaction_context*)malloc(sizeof(transaction_context));
			tc->gateway = gateway;
			tc->client = client;
			str_copy(&tc->buffer, buffer);
			pthread_create(&thread, NULL, start_transaction, (void*)tc);
			break;
		case CURRENT_STATE:
			LOG_DEBUG(("DEBUG: Current state message is received\n"));
			break;
		default:
			LOG_DEBUG(("Unknown/Unhandled message is received\n"));
			break;
		}

		// inference rules
		int turn_device_on = 0;
		strcpy(buffer, "");
		if(sensor_status[2] > sensor_status[0]) 
		{
			if(sensor_status[1] > sensor_status[0])
			{
				if(gateway->motion_state == 1)
				{
					if(gateway->door_state == 1 && gateway->key_state == 1)
					{
						turn_device_on = 0;
						strcpy(buffer, "User Entered - Turning off Security System\n");
						security_system_switch(gateway, 0);
						LOG_INFO(("INFO: User Entered - Turning off Security System\n"));
						LOG_SCREEN(("INFO: User Entered - Turning off Security System\n"));
					}
					if(gateway->key_state == 0)
					{
						strcpy(buffer, "Security Alert - Intruder Detected\n");
						LOG_INFO(("INFO: Security Alert\n"));
						LOG_SCREEN(("INFO: Security Alert\n"));
						gateway->motion_state = -1;
						gateway->door_state = -1;
						gateway->key_state = -1;
					}
				}
			}
			else
			{
				if(gateway->motion_state == 1 && gateway->door_state == 1)
				{
					strcpy(buffer, "Someone has entered, waiting for keychain event\n");
					LOG_INFO(("INFO: Someone has entered, waiting for keychain event\n"));
					LOG_SCREEN(("INFO: Someone has entered, waiting for keychain event\n"));
				}
			}
		}
		else if(sensor_status[0] > sensor_status[2] && sensor_status[2] == 3)
		{
			if(gateway->motion_state == 1)
			{
				if(gateway->door_state == 0)
				{
					turn_device_on = 1;
					strcpy(buffer, "User Existed - Turning on Security System\n");
					security_system_switch(gateway, 1);
					LOG_INFO(("INFO: User Existed - Turning on Security System\n"));
					LOG_SCREEN(("INFO: User Existed - Turning on Security System\n"));
				}
			}
		}
		else if(sensor_status[2] == 3)
		{
			if(gateway->motion_state == 1 && gateway->key_state == 0)
			{
				strcpy(buffer, "Security Alert - Someone else in the house\n");
				LOG_INFO(("INFO: Security Alert\n"));
				LOG_SCREEN(("INFO: Security Alert\n"));
				LOG_INFO(("INFO: last: Security Alert\n"));
				gateway->motion_state = -1;
				gateway->door_state = -1;
				gateway->key_state = -1;
			}
		}
		if(strlen(buffer))
		{
			pthread_t thread;
			transaction_context *tc = (transaction_context*)malloc(sizeof(transaction_context));
			tc->gateway = gateway;
			tc->client = client;
			str_copy(&tc->buffer, buffer);
			pthread_create(&thread, NULL, start_transaction, (void*)tc);

			if(turn_device_on)
			{
				strcpy(buffer, "");
				sprintf(buffer, "4----%-20s----%-10s----%-10lu----%-10s----%-10s\n",
									device_string[SECURITY_DEVICE],
									value_string[turn_device_on],
									msg->timestamp,
									"127.0.0.1",
									"1000");

				pthread_t thread;
				transaction_context *tc = (transaction_context*)malloc(sizeof(transaction_context));
				tc->gateway = gateway;
				tc->client = client;
				str_copy(&tc->buffer, buffer);
				pthread_create(&thread, NULL, start_transaction, (void*)tc);

			}
		}
		pthread_mutex_unlock(&gateway->mutex_lock);
	}
	return (NULL);
}

int get_replica_count(gateway_context *gateway)
{
	int count = 0;
	for(int index=0; index<gateway->client_count; index++)
	{
		if(gateway->clients[index]->type == REPLICA_GATEWAY)
		{
			count++;
		}
	}
	return count;
}

int two_phase_commit(gateway_context *gateway, gateway_client *client, char* commit_message)
{
	int return_value = E_FAILURE;
	int back_end_sock_fd = -1;
	char buffer[200] = {'\0'};
	char add_number_buff[200] = {'\0'};


	for(int index=0; index<gateway->client_count; index++)
	{
		if(gateway->clients[index]->type == BACK_TIER_GATEWAY)
		{
			back_end_sock_fd = gateway->clients[index]->comm_socket_fd;
			break;
		}
	}

	if(gateway->two_pc_state == NOTHING)
	{

		if(strcmp(commit_message, "") == 0)
		{
			LOG_ERROR(("Called from wrong place\n"));
			return E_INVALID_MESSAGE;
		}

		LOG_SCREEN(("2PC: TID=%d Transaction Started\n", gateway->transaction_number));
		gateway->two_pc_state = INIT;
		LOG_SCREEN(("2PC: Currently in INIT state\n"));
		gateway->transaction_number++;

		gateway->vote = 1;
		gateway->vote_count = 0;
		gateway->ack_count = 0;
		str_copy(&gateway->message, commit_message);

		sprintf(buffer, "Prepare:%d:%s", gateway->transaction_number, commit_message);
		LOG_SCREEN(("2PC: TID=%d Prepare message sent\n", gateway->transaction_number));
		gateway->two_pc_state = READY;

		return_value = send_message_to_replicas(gateway, buffer);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
		}

		LOG_SCREEN(("2PC: TID=%d Moving to waiting state\n", gateway->transaction_number));
		gateway->two_pc_state = WAIT;
		return E_SUCCESS;
	}

	if(gateway->two_pc_state == WAIT)
	{
		int vote = vote_from_replica(gateway, client);
		gateway->vote_count++;
		if(vote == 1)
		{
			gateway->vote = vote;
		}
		if(get_replica_count(gateway) != gateway->vote_count)
		{
			return E_SUCCESS;
		}
		LOG_SCREEN(("2PC: TID=%d All votes from participant received\n", gateway->transaction_number));
		if(gateway->vote == 1)
		{
			/* global commit */
			sprintf(buffer, "Commit:%d:Empty", gateway->transaction_number);
			return_value = send_message_to_replicas(gateway, buffer);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
			}
			gateway->two_pc_state = COMMIT;
			LOG_SCREEN(("2PC: TID=%d Global commit sent\n", gateway->transaction_number));

			sprintf(add_number_buff, "TID:%d----%s", gateway->transaction_number, gateway->message);
			return_value = send_msg_to_backend(back_end_sock_fd, add_number_buff);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
			}
		}
		else
		{
			/* global abort */
			sprintf(buffer, "Abort:%d:Empty", gateway->transaction_number);
			return_value = send_message_to_replicas(gateway, buffer);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to replicas\n"));
			}

			LOG_SCREEN(("2PC: TID=%d Global abort sent\n", gateway->transaction_number));
			gateway->two_pc_state = ABORT;

		}
	}
	if(gateway->two_pc_state == COMMIT
			|| gateway->two_pc_state == ABORT)
	{
		if(receive_ack_from_replica(gateway, client) == 0)
		{
			LOG_ERROR(("2PC: TID=%d Acknowledge not received from all replicas\n", gateway->transaction_number));
			return (E_SUCCESS);
		}
		gateway->ack_count++;
		if(get_replica_count(gateway) != gateway->ack_count)
		{
			return E_SUCCESS;
		}

		LOG_SCREEN(("2PC: TID=%d Transaction complete\n", gateway->transaction_number));
		gateway->two_pc_state = NOTHING;
	}
	return (E_SUCCESS);
}

int receive_ack_from_replica(gateway_context *gateway, gateway_client *client)
{
	int return_value;
	char *tokens[10] = {NULL};
	int count=0, error = 1;
	char *message = NULL;

	return_value = read_msg_from_frontend(client->comm_socket_fd, &message);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Unable to send message to replica"));
		return 0;
	}
	str_tokenize(message, ":", tokens, &count);
	if(count != 3)
	{
		LOG_ERROR(("ERROR: Wrong Message Received\n"));
		error = 0;
	}
	if(strcmp(tokens[0], "Ack") != 0
			|| atoi(tokens[1]) != gateway->transaction_number)
	{
		error = 0;
	}

	return error;
}

int vote_from_replica(gateway_context *gateway, gateway_client *client)
{
	int return_value;
	char *tokens[10] = {NULL};
	int count=0, vote = 1;
	char* message = NULL;

	return_value = read_msg_from_frontend(client->comm_socket_fd, &message);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Unable to send message to replica's"));
		return 0;
	}
	str_tokenize(message, ":", tokens, &count);
	if(count != 3)
	{
		LOG_ERROR(("ERROR: Wrong Message Received\n"));
		vote = 0;
	}
	if(strcmp(tokens[0], "Vote_Commit") != 0
			|| atoi(tokens[1]) != gateway->transaction_number)
	{
		vote = 0;
	}

	return vote;
}


int send_message_to_replicas(gateway_context *gateway, char *message)
{
	int return_value;
	int index;
	for(index=0; index<gateway->client_count; index++)
	{
		if(gateway->clients[index]->type == REPLICA_GATEWAY)
		{
			return_value = send_msg_to_backend(gateway->clients[index]->comm_socket_fd, message);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to replica's"));
				break;
			}
		}
	}
	if(index != (gateway->client_count))
		return (E_FAILURE);
	return (E_SUCCESS);
}

void print_state(gateway_context *gateway)
{
	int index;

	LOG_GATEWAY(("-----------------------------------------------\n"));
	for(index=0; index<gateway->client_count; index++)
	{
		/* Need to open this code

		if(gateway->clients[index].type == BACK_TIER_GATEWAY)
		{
			create message and send it the back end for inserting into the file
		}*/

		gateway_client *client = gateway->clients[index];
		if(client->type == SECURITY_DEVICE)
		{
			LOG_GATEWAY(("%d----%s:%s----%s----%s----%s\n",
									(int)(client&&0xFFFF),
									client->client_ip_address,
									client->client_port_number,
									device_string[client->type],
									client->area_id,
									state_string[client->state]));
		}
		else
		{
			LOG_GATEWAY(("%d----%s:%s----%s----%s----%d\n",
							(int)(client&&0xFFFF),
							client->client_ip_address,
							client->client_port_number,
							device_string[client->type],
							client->area_id,
							client->value));
		}
	}
	LOG_GATEWAY(("-----------------------------------------------\n"));
}

int create_gateway(gateway_handle* handle, gateway_create_params *params)
{
	gateway_context *gateway = NULL;
	int return_value = 0;

	gateway = (gateway_context*)malloc(sizeof(gateway_context));
	if(NULL == gateway)
	{
		LOG_ERROR(("ERROR: Out of memory\n"));
		return (E_OUT_OF_MEMORY);
	}
	memset(gateway, 0, sizeof(gateway_context));

	gateway->client_count = 0;
	gateway->logical_clock[0] = 0;
	gateway->logical_clock[1] = 0;
	gateway->logical_clock[2] = 0;
	gateway->logical_clock[3] = 0;
	gateway->switched = 0;

	gateway->primary_flag = 0;
	gateway->two_pc_state = NOTHING;
	gateway->transaction_number = 0;

	memset(gateway->buffered_messages, 0, 100*sizeof(void*));
	gateway->msg_queue = NULL;

	pthread_mutex_init(&gateway->mutex_lock, NULL);
	pthread_cond_init(&gateway->cond_lock, NULL);
	pthread_mutex_init(&gateway->transaction_lock, NULL);
	pthread_cond_init(&gateway->transaction_cond_lock, NULL);

	/* create network read thread */
	return_value = create_network_thread(&gateway->network_thread, params->gateway_ip_address);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Error in creating n/w read thread\n"));
		delete_gateway((gateway_handle)gateway);
		return (return_value);
	}

	/* create connection to server */
	return_value = create_server_socket(&gateway->server_socket_fd, params->gateway_ip_address, params->gateway_port_no);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Error in creating the socket\n"));
		delete_gateway((gateway_handle)gateway);
		return (return_value);
	}

	/* add socket to network read thread */
	return_value = add_socket(gateway->network_thread, gateway->server_socket_fd,  (void*)gateway, &accept_callback);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: add_socket() failed\n"));
		delete_gateway((gateway_handle)gateway);
		return (return_value);
	}

	if(strcmp(params->primary_gateway_ip_address, params->gateway_ip_address) == 0
				&& strcmp(params->primary_gateway_port_no, params->gateway_port_no)==0)
	{
		gateway->primary_flag = 1;
		LOG_SCREEN(("DEBUG: I'm Primary\n"));
	}
	else
	{
		/*establish connection to primary gateway */
		return_value = create_socket(&gateway->primary_gateway_socket_fd, params->primary_gateway_ip_address, params->primary_gateway_port_no);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: Error in connecting to primary gateway\n"));
			delete_gateway((gateway_handle)gateway);
			return (return_value);
		}

		/* add socket to network read thread */
		return_value = add_socket(gateway->network_thread, gateway->primary_gateway_socket_fd,  (void*)gateway, &primary_gateway_callback);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: add_socket() failed\n"));
			delete_gateway((gateway_handle)gateway);
			return (return_value);
		}

		message msg;
		msg.type = REGISTER;
		msg.u.s.type = REPLICA_GATEWAY;
		msg.u.s.ip_address = params->gateway_ip_address;
		msg.u.s.port_no = params->gateway_port_no;
		msg.u.s.area_id = "1";

		return_value = write_message(gateway->primary_gateway_socket_fd, gateway->logical_clock, &msg);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: write_message to primary gateway failed\n"));
			delete_gateway((gateway_handle)gateway);
			return (return_value);
		}

		/*establish connection to primary gateway for forwarding*/
		return_value = create_socket(&gateway->forwarding_socket_fd, params->primary_gateway_ip_address, params->primary_gateway_port_no);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: Error in connecting to primary gateway\n"));
			delete_gateway((gateway_handle)gateway);
			return (return_value);
		}

		/* add socket to network read thread */
		return_value = add_socket(gateway->network_thread, gateway->forwarding_socket_fd,  (void*)gateway, &forwarding_read_callback);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: add_socket() failed\n"));
			delete_gateway((gateway_handle)gateway);
			return (return_value);
		}

		msg.type = REGISTER;
		msg.u.s.type = FORWARD_GATEWAY;
		msg.u.s.ip_address = params->gateway_ip_address;
		msg.u.s.port_no = params->gateway_port_no;
		msg.u.s.area_id = "1";
		return_value = write_message(gateway->forwarding_socket_fd, gateway->logical_clock, &msg);
		if(E_SUCCESS != return_value)
		{
			LOG_ERROR(("ERROR: Error in connecting to primary gateway\n"));
			delete_gateway((gateway_handle)gateway);
			return (return_value);
		}

	}

	pthread_create(&gateway->message_handler_thread, NULL, message_handler, gateway);
	*handle = gateway;

	return (E_SUCCESS);
}
void delete_gateway(gateway_handle handle)
{
	/* release all the resources */
	gateway_context* gateway = (gateway_context*)handle;
	int index;

	if(gateway)
	{
		for(index=0; index<gateway->client_count; index++)
		{
			remove_socket(gateway->network_thread, gateway->clients[index]->comm_socket_fd);
			if(gateway->clients[index]->client_ip_address)
				free(gateway->clients[index]->client_ip_address);
			if(gateway->clients[index]->client_port_number)
				free(gateway->clients[index]->client_port_number);
			if(gateway->clients[index]->area_id)
				free(gateway->clients[index]->area_id);
			free(gateway->clients[index]);
		}

		if(gateway->network_thread)
		{
			delete_network_thread(gateway->network_thread);
		}
		if(gateway->server_socket_fd)
		{
			close_socket(gateway->server_socket_fd);
		}

		free(gateway);
	}
}

void* forwarding_read_callback(void* context)
{
	gateway_context *gateway = (gateway_context*)context;
	gateway->primary_flag = 1;
	return NULL;
}

void* primary_gateway_callback(void *context)
{
	int return_value;
	gateway_context *gateway = (gateway_context*)context;
	char* string = NULL;
	char *tokens[10];
	int count;
	char buffer[100] = {'\0'};

	char add_number_buff[250] = {'\0'};

	if(gateway->two_pc_state == NOTHING)
	{
		gateway->two_pc_state = INIT;
		gateway->transaction_number++;

	}

	/* 2 phase receiver protocol */
	return_value = read_msg_from_frontend(gateway->primary_gateway_socket_fd, &string);
	if(E_SUCCESS != return_value)
	{
		if(return_value == E_SOCKET_CONNECTION_CLOSED)
		{
			gateway->switched = 3;
			LOG_ERROR(("INFO: Primary gateway crash detected...\n"));
			remove_socket(gateway->network_thread, gateway->primary_gateway_socket_fd);
		}
		//exit(0);
		return (NULL);
	}

	str_tokenize(string, ":", tokens, &count);
	if(count != 3)
	{
		LOG_ERROR(("ERROR: Unknown message received\n"));
		return (NULL);
	}

	if(atoi(tokens[1]) != gateway->transaction_number)
	{
		LOG_ERROR(("ERROR: Processing other transaction: %d and got %d\n",
				gateway->transaction_number,
				atoi(tokens[1])));
		return (NULL);
	}

	if(gateway->two_pc_state == INIT)
	{
		if(strcmp(tokens[0], "Prepare") == 0)
		{
			LOG_SCREEN(("2PC: TID=%d Prepare message received\n", gateway->transaction_number));
			gateway->two_pc_state = READY;
			str_copy(&gateway->message, tokens[2]);
			LOG_SCREEN(("2PC: TID=%d Commit_vote_sent\n", gateway->transaction_number));
			sprintf(buffer, "Vote_Commit:%d:Empty", atoi(tokens[1]));
			return_value = send_msg_to_backend(gateway->primary_gateway_socket_fd, buffer);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
			}
			return (NULL);

		}
		else if(strcmp(tokens[0], "Commit") == 0)
		{
			LOG_ERROR(("2PC: TID=%d Commit Received and in INIT state\n", gateway->transaction_number));
			LOG_SCREEN(("2PC: TID=%d Abort_vote_sent\n", gateway->transaction_number));
			sprintf(buffer, "Abort_Commit:%d:Empty", atoi(tokens[1]));
			return_value = send_msg_to_backend(gateway->primary_gateway_socket_fd, add_number_buff);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
			}
			return (NULL);
		}
		else
		{
			LOG_ERROR(("ERROR: Unknown message received\n"));
			return (NULL);
		}
	}

	if(gateway->two_pc_state == READY)
	{
		if(strcmp(tokens[0], "Prepare") == 0)
		{
			LOG_ERROR(("ERROR: Prepare message received and in READY state\n"));
		}
		else if(strcmp(tokens[0], "Commit") == 0)
		{
			LOG_SCREEN(("2PC: TID=%d Global commit message received\n", gateway->transaction_number));
			gateway->two_pc_state = COMMIT;
			/* send to persistent storage */
			for(int index=0; index<gateway->client_count; index++)
			{
				if(gateway->clients[index]->type == BACK_TIER_GATEWAY)
				{
					sprintf(add_number_buff, "TID:%d----%s",gateway->transaction_number, gateway->message);
					return_value = send_msg_to_backend(gateway->clients[index]->comm_socket_fd, add_number_buff);
					if(E_SUCCESS != return_value)
					{
						LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
					}
				}
			}

			sprintf(buffer, "Ack:%d:Empty", atoi(tokens[1]));
			return_value = send_msg_to_backend(gateway->primary_gateway_socket_fd, buffer);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
			}
			LOG_SCREEN(("2PC: TID=%d Acknowledge for global commit sent\n", gateway->transaction_number));

			gateway->two_pc_state = NOTHING;
		}
		else if(strcmp(tokens[0], "Abort") == 0)
		{
			LOG_SCREEN(("2PC: TID=%d Global abort message received\n", gateway->transaction_number));
			gateway->two_pc_state = ABORT;

			sprintf(buffer, "Ack:%d:Empty", atoi(tokens[1]));
			return_value = send_msg_to_backend(gateway->primary_gateway_socket_fd, buffer);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Unable to send message to back-end\n"));
			}
			LOG_SCREEN(("2PC: TID=%d Acknowledge for global commit sent\n", gateway->transaction_number));

			gateway->two_pc_state = NOTHING;
		}
		else
		{
			LOG_ERROR(("ERROR: Unknown message received\n"));
			return (NULL);
		}

	}
	return (NULL);
}

void* accept_callback(void *context)
{
	int return_value = 0;
	gateway_context *gateway = NULL;
	gateway_client *client = NULL;

	gateway = (gateway_context*)context;

	client = (gateway_client*)malloc(sizeof(gateway_client));
	if(!client)
	{
		LOG_DEBUG(("DEBUG: Out of memory\n"));
		return (NULL);
	}

	client->type = UNKNOWN;
	client->gateway = context;
	client->comm_socket_fd = accept(gateway->server_socket_fd, (struct sockaddr*)NULL, NULL);
	if(client->comm_socket_fd < 0)
	{
		LOG_ERROR(("ERROR: Accept call failed\n"));
		free(client);
		return NULL;
	}

	gateway->clients[gateway->client_count] = client;
	gateway->client_count++;

	/* add socket to network read thread */
	return_value = add_socket(gateway->network_thread, client->comm_socket_fd,  (void*)client, &read_callback);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: add_socket() failed\n"));
		free(client);
		return (NULL);
	}
	client->connection_state = 1;

	return (NULL);
}

void* read_callback(void *context)
{
	gateway_client *client = (gateway_client*)context;
	gateway_context *gateway = client->gateway;
	int return_value = 0;
	message *msg = NULL;
	message_context *msg_context = NULL;
	//message snd_msg;
	//int index;
	//int flag_found = 0;
	int msg_logical_clock[CLOCK_SIZE];
	int buffer_message;
	int signal_message_handler;

	if(client->type == REPLICA_GATEWAY)
	{
		int return_value = two_phase_commit(gateway, client, "");
		if(return_value != E_INVALID_MESSAGE)
			return (NULL);
	}

	msg_context = (message_context*)malloc(sizeof(message_context));
	if(!msg_context)
	{
		LOG_ERROR(("Out of Memory\n"));
		return (NULL);
	}
	msg = (message*)malloc(sizeof(message));
	if(!msg)
	{
		LOG_ERROR(("Out of Memory\n"));
		return (NULL);
	}

	msg_context->client = client;
	msg_context->msg = msg;

	if(client->comm_socket_fd == -1)
		return NULL;
	return_value = read_message(client->comm_socket_fd, msg_logical_clock, msg);
	if(return_value != E_SUCCESS)
	{
		if(return_value == E_SOCKET_CONNECTION_CLOSED)
		{
			//LOG_ERROR(("ERROR: Connection closed for client: %s-%s-%s...\n",
			//		client->client_ip_address,
			//		client->client_port_number,
			//		client->area_id));
			client->comm_socket_fd = -1;
			remove_socket(client->gateway->network_thread, client->comm_socket_fd);
			client->connection_state = 0;
			if(client->type == REPLICA_GATEWAY)
			{
				//pthread_mutex_lock(&client->gateway->transaction_lock);
				LOG_SCREEN(("Secondary crash detected\n"));
				if(gateway->two_pc_state != NOTHING)
				{
					LOG_SCREEN(("INFO: Last transaction canceled\n"));
				}
				gateway->two_pc_state = NOTHING;
				//pthread_mutex_unlock(&client->gateway->transaction_lock);
			}
			LOG_ERROR(("ERROR: Connection closed for client\n"));
			/*
			index = 0;
			flag_found = 0;
			for(index=0; index<gateway->client_count; index++)
			{
				if(gateway->clients[index]->comm_socket_fd == client->comm_socket_fd)
				{
					flag_found = 1;
					break;
				}
			}
			if(flag_found == 1)
			{
				for(;index<gateway->client_count-1; index++)
				{
					gateway->clients[index] = gateway->clients[index+1];
				}
				gateway->client_count--;
			}
			if(client->client_ip_address)
				free(client->client_ip_address);
			if(client->client_port_number)
				free(client->client_port_number);
			if(client->area_id)
				free(client->area_id);
			free(client);
			return NULL;
			*/
		}
		LOG_ERROR(("ERROR: Error in read message, error: %d\n", return_value));
		return NULL;
	}

	if(gateway->primary_flag == 1 || msg->type == REGISTER)
	{

		if(client->type == FORWARD_GATEWAY)
		{
			for(int index=0; index<gateway->client_count; index++)
			{
				if(gateway->clients[index]->type == msg->u.s.type)
				{
					msg_context->client = gateway->clients[index];
					break;
				}
			}
		}

		pthread_mutex_lock(&gateway->mutex_lock);
		buffer_message = 0;
		signal_message_handler = 0;
		//adjust_clock(gateway->logical_clock, msg_logical_clock);

		if(gateway->switched !=0)
		{
			adjust_clock(gateway->logical_clock, msg_logical_clock);
			gateway->switched--;
			printf("----------->switching%d\n", gateway->switched);
			pthread_mutex_unlock(&gateway->mutex_lock);
			return NULL;
		}

		if(check_devlivery(gateway->logical_clock, msg_logical_clock, msg_context->client->type) )
		{
			/* correct order message */
			/* adjust clock */
			LOG_INFO(("------------------------------------\n"));
			LOG_INFO(("INFO: Message delivered\n"));
			LOG_INFO(("INFO: Process Clock : "));
			print_logical_clock(gateway->logical_clock);
			LOG_INFO((", timestamp: %lu, From %s:%s\n",
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number));
			add_queue(&gateway->msg_queue, msg_context);
			adjust_clock(gateway->logical_clock, msg_logical_clock);
			signal_message_handler = 1;

			LOG_INFO(("INFO: Message timest: "));
			print_logical_clock(msg_logical_clock);
			LOG_INFO((", timestamp: %lu, From %s:%s:Process: %d\n",
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number,
					msg_context->client->type));

			LOG_INFO(("INFO: Process Clock : "));
			print_logical_clock(gateway->logical_clock);
			LOG_INFO((", timestamp: %lu, From %s:%s\n",
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number));
			LOG_INFO(("------------------------------------\n"));

		}
		else
		{
			/* buffer message */
			buffer_message = 1;
		}
		if(buffer_message)
		{
			LOG_INFO(("---------------------------------------\n"));
			LOG_INFO(("DEBUG: Message buffered\n"))
			LOG_SCREEN(("DEBUG: Message buffered\n"));

			LOG_INFO(("INFO: Message timest: "));
			print_logical_clock(msg_logical_clock);
			LOG_INFO((", timestamp: %lu, From %s:%s:Process: %d\n",
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number,
					msg_context->client->type));

			LOG_INFO(("INFO: Process Clock : "));
			print_logical_clock(gateway->logical_clock);
			LOG_INFO((", timestamp: %lu, From %s:%s\n",
					msg->timestamp,
					client->client_ip_address,
					client->client_port_number));
			LOG_INFO(("---------------------------------------\n"));

			for(int index=0; index<100; index++)
			{
				if(gateway->buffered_messages[index] == NULL)
				{
					gateway->buffered_messages[index] = msg_context;
					break;
				}
			}
		}

		/* Check other buffered message */
		for(int index=0; index<100; index++)
		{
			if(gateway->buffered_messages[index])
			{
				message_context *temp_message = (message_context*)gateway->buffered_messages[index];
				if(check_devlivery(gateway->logical_clock, temp_message->msg->logical_clock, temp_message->client->type))
				{
					LOG_INFO(("-------------------------------------------\n"));
					LOG_INFO(("INFO: Message Delivered from buffered queue\n"));
					LOG_INFO(("INFO: Process Clock : "));
					print_logical_clock(gateway->logical_clock);
					LOG_INFO((", timestamp: %lu, From %s:%s\n",
							msg->timestamp,
							client->client_ip_address,
							client->client_port_number));

					add_queue(&gateway->msg_queue, temp_message);
					adjust_clock(gateway->logical_clock, temp_message->msg->logical_clock);
					gateway->buffered_messages[index] = NULL;
					index = 0;
					signal_message_handler = 1;

					LOG_INFO(("INFO: Message timest: "));
					print_logical_clock(temp_message->msg->logical_clock);
					LOG_INFO((", timestamp: %lu, From %s:%s:Process: %d\n",
							msg->timestamp,
							client->client_ip_address,
							client->client_port_number,
							msg_context->client->type));
					LOG_INFO(("INFO: Process Clock : "));
					print_logical_clock(gateway->logical_clock);
					LOG_INFO((", timestamp: %lu, From %s:%s\n",
							msg->timestamp,
							client->client_ip_address,
							client->client_port_number));
					LOG_INFO(("--------------------------------\n"));
				}
			}
		}
		if(signal_message_handler)
		{
			pthread_cond_signal(&gateway->cond_lock);
		}

		pthread_mutex_unlock(&gateway->mutex_lock);
	}
	else
	{
		//forward the message
		adjust_clock(gateway->logical_clock, msg_logical_clock);
		if(strcmp(msg->u.s.area_id, "2") == 0)
		{
			return_value = write_message(gateway->forwarding_socket_fd, msg_logical_clock, msg);
			if(return_value != E_SUCCESS)
			{
				LOG_ERROR(("ERROR: Unable to forward the message to primary gateway\n"));
			}
		}
	}

	return (NULL);
}

int set_interval(gateway_handle handle, int index, int interval)
{
	gateway_context *gateway = handle;
	message snd_msg;

	gateway->logical_clock[3]++;

	if(0 <= index && index < gateway->client_count)
	{
		snd_msg.type = SET_INTERVAL;
		snd_msg.u.value = interval;
		LOG_INFO(("INFO: Sending Clock\n"));
		print_logical_clock(gateway->logical_clock);
		return (write_message(gateway->clients[index]->comm_socket_fd, gateway->logical_clock, &snd_msg));
	}
	else
	{
		LOG_ERROR(("ERROR: Such sensor don't exists\n"));
	}
	return (E_FAILURE);
}

void print_sensors(gateway_handle handle)
{
	int index;
	gateway_context *gateway = handle;

	printf("----------------List of sensor---------------\n");
	printf("<ID>-<IP_Address>-<Port_Number>-<AreaId>\n");
	for(index=0; index<gateway->client_count; index++)
	{
		if(gateway->clients[index]->type == DOOR_SENSOR)
		{
			printf("%d:%s-%s-%s\n",
					index,
					gateway->clients[index]->client_ip_address,
					gateway->clients[index]->client_port_number,
					gateway->clients[index]->area_id);
		}
	}
}
