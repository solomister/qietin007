#include "stdio-task.h"
#include "stdio.h"
#include "pico/stdlib.h"

#define COMMAND_BUF_LEN 128

char command[COMMAND_BUF_LEN] = {0};

int command_buf_idx;

void stdio_task_init()
{
	command_buf_idx = 0;
}

char* stdio_task_handle()
{
	int symbol = getchar_timeout_us(0);
	if (symbol == PICO_ERROR_TIMEOUT)
	{
		return NULL;
	}

	putchar(symbol);

	if (symbol == '\r' || symbol == '\n')
	{
		command[command_buf_idx] = '\0';
		command_buf_idx = 0;
		
		printf("received string: '%s'\n", command);

	return command;
	}

	if (command_buf_idx >= COMMAND_BUF_LEN - 1)
	{
		command_buf_idx = 0;
		return NULL;
	}

	command[command_buf_idx] = symbol;
	command_buf_idx++;
	return NULL;
}