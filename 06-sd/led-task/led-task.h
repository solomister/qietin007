#pragma once

#include "stdint.h"

typedef enum {
    LED_STATE_OFF   = 0,
    LED_STATE_ON    = 1,
    LED_STATE_BLINK = 2,
} led_state_t;

void led_task_init();
void led_task_handle();

void led_task_set_state(led_state_t state);
void led_task_set_blink_period_ms(uint32_t period_ms);
