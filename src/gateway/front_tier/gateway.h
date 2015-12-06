/*
 * gateway.h
 *
 *  Created on: Oct 1, 2015
 *      Author: Abhijeet Wadkar, Devendra Dahiphale
 */

#ifndef GATEWAY_H_
#define GATEWAY_H_

#include <pthread.h>

#include "network_functions.h"
#include "network_read_thread.h"
#include "message.h"
#include "queue.h"
#include "two_phase_commit.h"

typedef void* gateway_handle;

typedef struct gateway_context gateway_context;

typedef struct gateway_client
{
	gateway_context *gateway;
	int comm_socket_fd;
	device_type type;
	char* client_ip_address;
	char* client_port_number;
	char* area_id;
	int state;
	int connection_state;
	int value;
}gateway_client;

typedef struct gateway_create_params
{
	char *gateway_ip_address;
	char *gateway_port_no;
	char *primary_gateway_ip_address;
	char *primary_gateway_port_no;
}gateway_create_params;

typedef struct gateway_context
{
	int primary_flag;
	gateway_create_params *gateway_params;
	int server_socket_fd;
	network_thread_handle network_thread;
	int client_count;
	gateway_client *clients[100];
	int logical_clock[CLOCK_SIZE];
	int key_state, motion_state, door_state;
	void *buffered_messages	[100];
	queue *msg_queue;
	pthread_mutex_t mutex_lock;
	pthread_cond_t cond_lock;
	pthread_t message_handler_thread;
	int primary_gateway_socket_fd;
	state two_pc_state;
	int transaction_number;
}gateway_context;

int create_gateway(gateway_handle*, gateway_create_params*);
void delete_gateway(gateway_handle);
int set_interval(gateway_handle handle, int index, int interval);
void print_sensors(gateway_handle handle);

#endif /* GATEWAY_H_ */
