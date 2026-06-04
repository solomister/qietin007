#pragma once

#include "stdint.h"

typedef void(*command_callback_t)(const char* args);

typedef struct
{
    const char* command_name;
    command_callback_t command_callback;
    const char* command_help;
} api_t;

void protocol_task_init(api_t* device_api);
void protocol_task_handle(char* command_string);

void protocol_task_print_help();
