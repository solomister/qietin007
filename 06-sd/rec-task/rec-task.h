#pragma once

#include "stdbool.h"

void rec_task_init(void);
void rec_start_callback(const char *args);
void rec_stop_callback(const char *args);

/* Возвращает true пока Core 1 ведёт запись.
   SD-команды на Core 0 должны проверять это перед f_mount. */
bool rec_is_running(void);
