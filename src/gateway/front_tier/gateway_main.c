/*
 * gateway_main.c
 *
 *  Created on: Oct 1, 2015
 *      Author: Abhijeet Wadkar, Devendra Dahiphale
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <signal.h>

#include "common.h"
#include "gateway.h"
#include "logger.h"
#include "string_helper_functions.h"
#include "error_codes.h"

#define TOKEN_MAX 10

void signal_handler(int signum)
{
        if(signum == SIGPIPE)
        {
                // just for debugging purpose
                LOG_INFO(("in signal hanlder handling SIGPIPE"));
        }
}
int main(int argc, char*argv[])
{
	char *conf_file_name = NULL;
	FILE *conf_file_pointer = NULL;
	char line[LINE_MAX] = {'\0'};
	char *tokens[TOKEN_MAX] = {NULL};
	int count = 0;
	gateway_create_params gateway_device = {NULL, NULL};
	gateway_handle gateway = NULL;
	int return_value = E_FAILURE;

	signal(SIGPIPE, signal_handler);
	LOG_DEBUG(("DEBUG: Number of arguments are: %d\n", argc));

	if(argc<3)
	{
		LOG_ERROR(("ERROR: Please provide configuration file name\n"));
		return (0);
	}

	conf_file_name = argv[1];

	LOG_DEBUG(("DEBUG: Configuration File Name is %s\n", conf_file_name));

	conf_file_pointer = fopen(conf_file_name, "r");
	if(!conf_file_pointer)
	{
		LOG_ERROR(("ERROR: Error in opening configuration file\n"));
		return (0);
	}

	return_value = log_open_output_file(argv[2]);
	if(E_SUCCESS != return_value)
	{
		LOG_ERROR(("ERROR: Unable to open the file for writing\n"));
		return (0);
	}

	/* Read line */
	if(fgets(line, LINE_MAX, conf_file_pointer) == NULL)
	{
		LOG_DEBUG(("DEBUG: Cleanup and return\n"));
		fclose(conf_file_pointer);
		LOG_ERROR(("ERROR: Wrong configuration file\n"));
		return (0);
	}
	str_tokenize(line, ",:\n\r", tokens, &count);
	if(count<2)
	{
		LOG_ERROR(("Wrong configuration file\n"));
		fclose(conf_file_pointer);
		return (0);
	}

	str_copy(&gateway_device.gateway_ip_address, tokens[0]);
	str_copy(&gateway_device.gateway_port_no, tokens[1]);
	LOG_DEBUG(("IP Address: %s\n", gateway_device.gateway_ip_address));
	LOG_DEBUG(("Port No: %s\n", gateway_device.gateway_port_no));


	/* Read line */
	if(fgets(line, LINE_MAX, conf_file_pointer) == NULL)
	{
		LOG_DEBUG(("DEBUG: Cleanup and return\n"));
		fclose(conf_file_pointer);
		LOG_ERROR(("ERROR: Wrong configuration file\n"));
		return (0);
	}
	str_tokenize(line, ",:\n\r", tokens, &count);
	if(count<3)
	{
		LOG_ERROR(("Wrong configuration file\n"));
		fclose(conf_file_pointer);
		return (0);
	}

	str_copy(&gateway_device.primary_gateway_ip_address, tokens[1]);
	str_copy(&gateway_device.primary_gateway_port_no, tokens[2]);
	LOG_SCREEN(("Primary Gateway IP Address: %s\n", gateway_device.primary_gateway_ip_address));
	LOG_SCREEN(("Primary Gateway Port No: %s\n", gateway_device.primary_gateway_port_no));

	return_value = create_gateway(&gateway, &gateway_device);
	if(E_SUCCESS != return_value)
	{
		LOG_SCREEN(("ERROR: Unable to create gateway\n"));
		free(gateway_device.gateway_ip_address);
		free(gateway_device.gateway_port_no);
		fclose(conf_file_pointer);
		return (0);
	}

	LOG_SCREEN(("INFO: Gateway Front End started successfully\n"));
	LOG_SCREEN(("INFO: Output is Redirected to: %s\n", argv[2]));

	char choice;
	printf("Press any key to exit...\n");
	scanf("%c", &choice);

	delete_gateway(gateway);

	free(gateway_device.gateway_ip_address);
	free(gateway_device.gateway_port_no);
	fclose(conf_file_pointer);
	logger_close();
	return (0);
}
