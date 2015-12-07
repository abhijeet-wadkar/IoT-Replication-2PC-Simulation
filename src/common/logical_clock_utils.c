/*
 * logical_clock_utils.c
 *
 *  Created on: Nov 10, 2015
 *      Author: abhijeet
 */

#include "logical_clock_utils.h"
#include "logger.h"

static int max(int a, int b)
{
	if(a>b)
		return (a);
	return (b);
}

int check_devlivery(int local_clock[CLOCK_SIZE], int msg_clock[CLOCK_SIZE], int client_no)
{
	int index, flag = 0;

	for(index=0; index<CLOCK_SIZE; index++)
	{
		if(msg_clock[index]!=0)
			break;
	}
	if(index==CLOCK_SIZE)
		return 1;

	if(msg_clock[client_no] != local_clock[client_no]+1)
	{
		return 0;
	}

	for(index=0; index<CLOCK_SIZE; index++)
	{
		if(msg_clock[index] > local_clock[index]  && index!=client_no)
		{
			return 0;
		}
	}
	return 1;
}

void adjust_clock(int local_clock[CLOCK_SIZE], int msg_clock[CLOCK_SIZE])
{
	for(int index=0; index<CLOCK_SIZE; index++)
	{
		local_clock[index] = max(local_clock[index], msg_clock[index]);
	}
}

void print_logical_clock(int logical_clock[CLOCK_SIZE])
{
	LOG_INFO(("CLOCK"));
	LOG_INFO(("<"));
	for(int index=0; index<CLOCK_SIZE; index++)
	{
		if(index+1==CLOCK_SIZE)
		{
			LOG_INFO(("%d", logical_clock[index]));
		}
		else
		{
			LOG_INFO(("%d,", logical_clock[index]));
		}
	}
	LOG_INFO((">"));
}

void print_logical_clock_to_screen(int logical_clock[CLOCK_SIZE])
{
	LOG_INFO(("CLOCK"));
	LOG_INFO(("<"));
	for(int index=0; index<CLOCK_SIZE; index++)
	{
		if(index+1==CLOCK_SIZE)
		{
			LOG_INFO(("%d", logical_clock[index]));
		}
		else
		{
			LOG_INFO(("%d,", logical_clock[index]));
		}
	}
	LOG_INFO((">"));
}

