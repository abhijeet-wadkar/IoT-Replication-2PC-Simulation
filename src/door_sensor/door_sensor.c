/*
 * sensor.c
 *
 *  Created on: Sep 27, 2015
 *      Author: Abhijeet Wadkar, Devendra Dahiphale
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

#include "common.h"
#include "door_sensor.h"
#include "error_codes.h"
#include "logger.h"
#include "network_functions.h"
#include "message.h"
#include "string_helper_functions.h"
#include "logical_clock_utils.h"

static void* accept_callback(void *context);
static void* read_callback(void *context);
static void* read_callback_peer(void *context);
static void* set_value_thread(void *context);
void sighand(int signo);

int create_sensor(sensor_handle *handle, sensor_create_params *params)
{
	sensor_context *sensor = NULL;
	int return_value = E_FAILURE;

	sensor = (sensor_context*)malloc(sizeof(sensor_context));
	if(NULL == sensor)
	{
		delete_sensor((sensor_handle)sensor);
		LOG_ERROR(("ERROR: Out of memory\n"));
		return (E_OUT_OF_MEMORY);
	}

	memset(sensor, 0, sizeof(sensor_context));
	sensor->interval = 5;
	sensor->sensor_params = params;
	sensor->clock = 0;
	sensor->value = 0;
	sensor->run = 1;
	sensor->recv_peer_count = 0;
	sensor->send_peer_count = 0;
	sensor->active_gateway = 1;
	sensor->gatway_decided = 0;

	pthread_mutex_init(&sensor->mutex_lock, NULL);

	sensor->sensor_value_file_pointer = fopen(params->sensor_value_file_name, "r");
	if(!sensor->sensor_value_file_pointer)
	{
		LOG_ERROR(("Unable to open sensor value input file\n"));
		delete_sensor(sensor);
		return (E_FAILURE);
	}

	/* create network read thread */
	return_value = create_network_thread(&sensor->network_thread, params->sensor_ip_address);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Error in creating n/w read thread\n"));
		delete_sensor((sensor_handle)sensor);
		return (return_value);
	}

	/* create connection to server */
	return_value = create_server_socket(&sensor->server_socket_fd, params->sensor_ip_address, params->sensor_port_no);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Error in creating the socket\n"));
		delete_sensor((sensor_handle)sensor);
		return (return_value);
	}

	/* add socket to network read thread */
	return_value = add_socket(sensor->network_thread, sensor->server_socket_fd,  (void*)sensor, &accept_callback);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: add_socket() failed\n"));
		delete_sensor((sensor_handle)sensor);
		return (return_value);
	}

	/* create connection to server */
	return_value = create_socket(&sensor->socket_fd[0], params->gateway_ip_address, params->gateway_port_no);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Connection to Server failed\n"));
		delete_sensor((sensor_handle)sensor);
		return (return_value);
	}

	/* add socket to network read thread */
	return_value = add_socket(sensor->network_thread, sensor->socket_fd[0],  (void*)sensor, &read_callback);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: add_socket() filed\n"));
		delete_sensor((sensor_handle)sensor);
		return (return_value);
	}


	message msg;

	/* register sensor with gateway */
	msg.type = REGISTER;
	msg.u.s.type = DOOR_SENSOR;
	msg.u.s.ip_address = sensor->sensor_params->sensor_ip_address;
	msg.u.s.port_no = sensor->sensor_params->sensor_port_no;
	msg.u.s.area_id = sensor->sensor_params->sensor_area_id;

	return_value = write_message(sensor->socket_fd[0], sensor->logical_clock, &msg);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Error in registering sensor\n"));
		return (E_FAILURE);
	}

	struct sigaction        actions;
	memset(&actions, 0, sizeof(actions));
	sigemptyset(&actions.sa_mask);
	actions.sa_flags = 0;
	actions.sa_handler = sighand;
	sigaction(SIGALRM,&actions,NULL);

	*handle = sensor;

	LOG_SCREEN(("INFO: Waiting for Peers to connect...\n"));

	return (E_SUCCESS);
}

void delete_sensor(sensor_handle handle)
{
	/* release all the resources */
	sensor_context* sensor = (sensor_context*)handle;

	if(sensor)
	{
		if(sensor->network_thread)
		{
			delete_network_thread(sensor->network_thread);
		}
		if(sensor->socket_fd)
		{
			close_socket(sensor->socket_fd[0]);
			close_socket(sensor->socket_fd[1]);
		}
		if(sensor->set_value_thread)
		{
			sensor->run = 0;
			pthread_kill(sensor->set_value_thread, SIGALRM);
			pthread_join(sensor->set_value_thread, NULL);
		}

		free(sensor);
	}
}

static void* accept_callback(void *context)
{
	int return_value = 0;
	sensor_context *sensor = NULL;
	peer *client = NULL;

	sensor = (sensor_context*)context;

	client = (peer*)malloc(sizeof(peer));
	if(!client)
	{
		LOG_DEBUG(("DEBUG: Out of memory\n"));
		return (NULL);
	}

	client->sensor = context;
	client->comm_socket_fd = accept(sensor->server_socket_fd, (struct sockaddr*)NULL, NULL);
	if(client->comm_socket_fd < 0)
	{
		LOG_ERROR(("ERROR: Accept call failed\n"));
		free(client);
		return NULL;
	}

	sensor->recv_peer[sensor->recv_peer_count] = client;
	sensor->recv_peer_count++;

	/* add socket to network read thread */
	return_value = add_socket(sensor->network_thread, client->comm_socket_fd,  (void*)client, &read_callback_peer);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: add_socket() failed\n"));
		free(client);
		return (NULL);
	}
	//client->connection_state = 1;

	LOG_SCREEN(("INFO: All Peers connected successfully...\n"));
	if(sensor->send_peer_count == 2 && sensor->recv_peer_count ==2)
	{
		sensor->active_gateway = sensor->gatway_decided;
		printf("INFO: Active gateway = %d\n", sensor->active_gateway);
		pthread_create(&sensor->set_value_thread, NULL, &set_value_thread, sensor);
	}
	return (NULL);
}

static void* read_callback_peer(void *context)
{
	int return_value = 0;
	sensor_context *sensor = NULL;
	peer *client = (peer*)context;
	message msg;

	sensor = client->sensor;

	int msg_logical_clock[CLOCK_SIZE];

	return_value = read_message(client->comm_socket_fd, msg_logical_clock, &msg);
	if(return_value != E_SUCCESS)
	{
		if(return_value == E_SOCKET_CONNECTION_CLOSED)
		{
			client->comm_socket_fd = -1;
	//		LOG_ERROR(("ERROR: Socket connection from server closed...\n"));
		}
	//	LOG_ERROR(("ERROR: Error in read message\n"));
		return NULL;
	}

	if(msg.type == REGISTER)
	{
		client->ip_address = msg.u.s.ip_address;
		client->port_no = msg.u.s.port_no;
		return (NULL);
	}

	pthread_mutex_lock(&sensor->mutex_lock);
	adjust_clock(sensor->logical_clock, msg_logical_clock);
	pthread_mutex_unlock(&sensor->mutex_lock);

	LOG_INFO(("INFO: Event Received: "));
	print_logical_clock(sensor->logical_clock);
	LOG_INFO((", timestamp: %lu, From %s:%s\n", msg.timestamp, client->ip_address, client->port_no));

	return (NULL);
}

static void* read_callback(void *context)
{
	sensor_context *sensor = (sensor_context*)context;
	int return_value = 0;
	message msg, snd_msg;
	peer *client = NULL;
	int msg_logical_clock[CLOCK_SIZE];

	return_value = read_message(sensor->socket_fd[sensor->active_gateway -1], msg_logical_clock, &msg);
	if(return_value != E_SUCCESS)
	{
		if(return_value == E_SOCKET_CONNECTION_CLOSED)
		{
			//LOG_ERROR(("ERROR: Socket connection from server closed...\n"));
			remove_socket(sensor->network_thread, sensor->socket_fd[sensor->active_gateway -1]);
			//close_socket(sensor->socket_fd[sensor->active_gateway -1]);
			sensor->socket_fd[sensor->active_gateway -1] = -1;

			if(sensor->socket_fd[0] == -1 && sensor->socket_fd[1] == -1)
			{
				LOG_ERROR(("ERROR: Both gateway terminated, so exiting....\n"));
				exit(0);
			}

			sensor->active_gateway == 1?(sensor->active_gateway=2):(sensor->active_gateway = 1);

			/* register sensor with gateway */
			snd_msg.type = REGISTER;
			snd_msg.u.s.type = DOOR_SENSOR;
			snd_msg.u.s.ip_address = sensor->sensor_params->sensor_ip_address;
			snd_msg.u.s.port_no = sensor->sensor_params->sensor_port_no;
			snd_msg.u.s.area_id = sensor->sensor_params->sensor_area_id;

			printf("Again registering to %d\n", sensor->active_gateway);

			return_value = write_message(sensor->socket_fd[sensor->active_gateway], sensor->logical_clock, &snd_msg);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Error in registering sensor\n"));
				return (E_FAILURE);
			}
		}
		//LOG_ERROR(("ERROR: Error in read message\n"));
		return NULL;
	}

	switch(msg.type)
	{
	case SET_INTERVAL:
		LOG_INFO(("INFO: SetInterval message received\n"));
		sensor->interval = msg.u.value;
		break;
	case REGISTER:
		LOG_INFO(("INFO: Register received from gateway\n"));

		if(msg.u.s.type == REPLICA_GATEWAY)
		{
			/* create connection to server */
			return_value = create_socket(&sensor->socket_fd[1], msg.u.s.ip_address, msg.u.s.port_no);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Connection to Server failed\n"));
				return NULL;
			}

			/* add socket to network read thread */
			return_value = add_socket(sensor->network_thread, sensor->socket_fd[1],  (void*)sensor, &read_callback);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: add_socket() filed\n"));
				return NULL;
			}

			/* register sensor with gateway */
			snd_msg.type = REGISTER;
			snd_msg.u.s.type = DOOR_SENSOR;
			snd_msg.u.s.ip_address = sensor->sensor_params->sensor_ip_address;
			snd_msg.u.s.port_no = sensor->sensor_params->sensor_port_no;
			snd_msg.u.s.area_id = sensor->sensor_params->sensor_area_id;

			return_value = write_message(sensor->socket_fd[1], sensor->logical_clock, &snd_msg);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Error in registering sensor\n"));
				return (E_FAILURE);
			}

		}
		else
		{
			client = (peer*)malloc(sizeof(peer));
			if(!client)
			{
				LOG_ERROR(("ERROR: Out of memory"));
				return (NULL);
			}

			client->ip_address = msg.u.s.ip_address;
			client->port_no = msg.u.s.port_no;

			// check for the serving gateway
			if(!(sensor->gatway_decided))
			{
				if(*(msg.u.s.area_id) == '1')
				{
					// serving gateway is one
					sensor->gatway_decided = 1;
					str_copy(&sensor->sensor_params->sensor_area_id, "1");
				}
				else
				{
					// only a sensor and a device are served by gateway 1
					// other sensors will be served by gateway 2 or replica gateway
					sensor->gatway_decided = 2;
					str_copy(&sensor->sensor_params->sensor_area_id, "2");
				}
			}
			LOG_INFO(("INFO: New Peer %s, %s\n", client->ip_address, client->port_no));

			/* create connection to server */
			return_value = create_socket(&client->comm_socket_fd,
					msg.u.s.ip_address,
					msg.u.s.port_no);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Connection to Server failed\n"));
			}

			/* add socket to network read thread */
			return_value = add_socket(sensor->network_thread,
					client->comm_socket_fd,
					(void*)client,
					&read_callback_peer);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: add_socket() filed\n"));
			}
			sensor->send_peer[sensor->send_peer_count] = client;
			sensor->send_peer_count++;

			snd_msg.type = REGISTER;
			snd_msg.u.s.type = DOOR_SENSOR;
			snd_msg.u.s.ip_address = sensor->sensor_params->sensor_ip_address;
			snd_msg.u.s.port_no = sensor->sensor_params->sensor_port_no;
			snd_msg.u.s.area_id = sensor->sensor_params->sensor_area_id;
			return_value = write_message(client->comm_socket_fd, sensor->logical_clock, &snd_msg);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("Error in communication\n"));
			}

			if(sensor->send_peer_count == 2 && sensor->recv_peer_count == 2)
			{
				sensor->active_gateway = sensor->gatway_decided;
				printf("INFO: Active gateway = %d\n", sensor->active_gateway);
				pthread_create(&sensor->set_value_thread, NULL, &set_value_thread, sensor);
			}
		}

		break;
	default:
		LOG_INFO(("INFO: Unknown/Unhandled message was received\n"));
		break;
	}

	return NULL;
}

void sighand(int signo)
{
	LOG_DEBUG(("DEBUG: EXITING SET_VALUE_THREAD\n"));
}

void* set_value_thread(void *context)
{
	sensor_context *sensor = NULL;
	message msg;
	int return_value;
	char *tokens[10];
	char line[LINE_MAX];
	int count = 0;
	int start, value;

	sensor = (sensor_context*)context;

	msg.type = CURRENT_VALUE;
	if(fgets(line, LINE_MAX, sensor->sensor_value_file_pointer) == NULL)
	{
		LOG_DEBUG(("DEBUG: Seeking to beginning of file"));
		rewind(sensor->sensor_value_file_pointer);
		sensor->clock = 0;
	}
	str_tokenize(line, ",;\n\r", tokens, &count);
	start = atoi(tokens[0]);
	if(!strcmp (tokens[1], "Open"))
	{
		value = 1;
	}
	else
	{
		value = 0;
	}
	sensor->value = value;
	msg.u.s.type = DOOR_SENSOR;
	str_copy(&msg.u.s.ip_address, sensor->sensor_params->sensor_ip_address);
	str_copy(&msg.u.s.port_no, sensor->sensor_params->sensor_port_no);
	str_copy(&msg.u.s.area_id, sensor->active_gateway == 1? "1":"2");
	while(sensor->run)
	{
		msg.u.value = sensor->value;
		msg.timestamp = time(NULL);

		pthread_mutex_lock(&sensor->mutex_lock);

		sensor->logical_clock[DOOR_SENSOR]++;

		LOG_SCREEN(("INFO: Event Sent, "));
		LOG_INFO(("INFO: Event Sent, "));
		print_logical_clock(sensor->logical_clock);
		print_logical_clock_to_screen(sensor->logical_clock);
		LOG_INFO(("timestamp: %lu, Door: %s\n", msg.timestamp, tokens[1]));
		LOG_SCREEN(("timestamp: %lu, Door: %s\n", msg.timestamp, tokens[1]));

		if(sensor->socket_fd[sensor->active_gateway - 1] != -1)
		{
			return_value = write_message(sensor->socket_fd[sensor->active_gateway - 1], sensor->logical_clock, &msg);
			if(E_SUCCESS != return_value)
			{
				LOG_ERROR(("ERROR: Error in sending sensor temperature value to gateway\n"));
			}
		}

		for(int index=0; index<sensor->send_peer_count; index++)
		{
			if(sensor->send_peer[index]->comm_socket_fd != -1)
			{
				return_value = write_message(sensor->send_peer[index]->comm_socket_fd,
						sensor->logical_clock,
						&msg);
				if(E_SUCCESS != return_value)
				{
					LOG_ERROR(("ERROR: Error in sending sensor temperature value to peer\n"));
				}
			}
		}
		pthread_mutex_unlock(&sensor->mutex_lock);

		/* Figure out the value from file */
		if(fgets(line, LINE_MAX, sensor->sensor_value_file_pointer) == NULL)
		{
			LOG_DEBUG(("DEBUG: Seeking to beginning of file"));
			rewind(sensor->sensor_value_file_pointer);
			sensor->clock = 0;
			if(fgets(line, LINE_MAX, sensor->sensor_value_file_pointer) == NULL)
			{
				LOG_ERROR(("ERROR: Wrong sensor temperature value file\n"));
			}
		}

		str_tokenize(line, ",;\n\r", tokens, &count);
		if(count != 2)
		{
			LOG_ERROR(("ERROR: Wrong sensor temperature value file\n"));
			break;
		}

		start = atoi(tokens[0]);
		if(!strcmp (tokens[1], "Open"))
		{
			value = 1;
		}
		else
		{
			value = 0;
		}
		sensor->value = value;
		sensor->interval = start - sensor->clock;
		sleep(sensor->interval);
		sensor->clock += sensor->interval;
	}

	LOG_DEBUG(("Exiting SetValueThread...\n"));
	return (NULL);
}
