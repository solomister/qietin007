#pragma once

#include "sd-driver.h"

void sd_task_init(void);

/* Инициализировать карту если ещё не сделано. Быстро возвращает SD_OK если уже готово. */
sd_err_t sd_task_ensure_init(void);

/* Получить указатель на объект карты (для diskio.c). NULL или type==UNKNOWN — не готова. */
const sd_t *sd_task_card(void);

void sd_info_callback(const char *args);
void sd_debug_callback(const char *args);
void sd_test_callback(const char *args);
void sd_write_block_callback(const char *args);
void sd_read_block_callback(const char *args);
