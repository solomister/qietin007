#pragma once

#include "stdint.h"

typedef void (*bme280_i2c_read)(uint8_t* buffer, uint16_t length);
typedef void (*bme280_i2c_write)(uint8_t* data, uint16_t size);

typedef struct
{
    bme280_i2c_read i2c_read;
    bme280_i2c_write i2c_write;
} bme280_ctx_t;

typedef struct
{
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_data_t;

void bme280_init(bme280_i2c_read i2c_read, bme280_i2c_write i2c_write);
void bme280_read_calibration();

void bme280_read_regs(uint8_t start_reg_address, uint8_t* buffer, uint8_t length);
void bme280_write_reg(uint8_t reg_address, uint8_t value);

uint16_t bme280_read_temp_raw();
uint16_t bme280_read_hum_raw();
uint16_t bme280_read_press_raw();

float bme280_read_temp_dC();
float bme280_read_press_Pa();
float bme280_read_hum_pct();
