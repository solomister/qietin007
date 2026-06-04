#include "bme280-driver.h"
#include "bme280-regs.h"

#include "stdio.h"

static bme280_ctx_t bme280_ctx = {0};
static bme280_calib_data_t calib = {0};

void bme280_init(bme280_i2c_read i2c_read, bme280_i2c_write i2c_write)
{
    bme280_ctx.i2c_read = i2c_read;
    bme280_ctx.i2c_write = i2c_write;

    uint8_t id_reg_buf[1] = {0};

    while (*id_reg_buf != BME280_REG_id_RS)
    {
        bme280_read_regs(BME280_REG_id, id_reg_buf, sizeof(id_reg_buf));
        printf("Fail to read 0x60 from 0x%X reg of bme280!", BME280_REG_id);
    }

    uint8_t config_reg_value = 0;
    config_reg_value |= (0b0 << 0);   // spi3w_en[0:0] = false
    config_reg_value |= (0b000 << 2); // filter[4:2] = Filter off
    config_reg_value |= (0b001 << 5); // t_sb[7:5] = 62.5 ms
    bme280_write_reg(BME280_REG_config, config_reg_value);

    uint8_t ctrl_hum_reg_value = 0;
    ctrl_hum_reg_value |= (0b001 << 0); // osrs_h[2:0] = oversampling 1
    bme280_write_reg(BME280_REG_ctrl_hum, ctrl_hum_reg_value);

    uint8_t ctrl_meas_reg_value = 0;
    ctrl_meas_reg_value |= (0b11 << 0);  // mode[1:0] = Normal mode
    ctrl_meas_reg_value |= (0b001 << 2); // osrs_t[4:2] = oversampling 1
    ctrl_meas_reg_value |= (0b001 << 5); // osrs_t[7:5] = oversampling 1
    bme280_write_reg(BME280_REG_ctrl_meas, ctrl_meas_reg_value);

    bme280_read_calibration();
}

void bme280_read_regs(uint8_t start_reg_address, uint8_t *buffer, uint8_t length)
{
    uint8_t data[1] = {start_reg_address};
    bme280_ctx.i2c_write(data, sizeof(data));
    bme280_ctx.i2c_read(buffer, length);
}

void bme280_write_reg(uint8_t reg_address, uint8_t value)
{
    uint8_t data[2] = {reg_address, value};
    bme280_ctx.i2c_write(data, sizeof(data));
}

uint16_t bme280_read_temp_raw()
{
    uint8_t read[2] = {0};
    bme280_read_regs(BME280_REG_temp_msb, read, sizeof(read));
    uint16_t value = ((uint16_t)read[0] << 8) | ((uint16_t)read[1]);
    return value;
}

uint16_t bme280_read_hum_raw()
{
    uint8_t read[2] = {0};
    bme280_read_regs(BME280_REG_hum_msb, read, sizeof(read));
    uint16_t value = ((uint16_t)read[0] << 8) | ((uint16_t)read[1]);
    return value;
}

uint16_t bme280_read_press_raw()
{
    uint8_t read[2] = {0};
    bme280_read_regs(BME280_REG_press_msb, read, sizeof(read));
    uint16_t value = ((uint16_t)read[0] << 8) | ((uint16_t)read[1]);
    return value;
}

void bme280_read_calibration()
{
    uint8_t data[34];

    bme280_read_regs(BME280_REG_DIG_T1, data, 24);
    bme280_read_regs(BME280_REG_DIG_H2, &data[24], 7);
    bme280_read_regs(BME280_REG_DIG_H1, &data[23], 1);

    calib.dig_T1 = (uint16_t)(data[1] << 8) | data[0];
    calib.dig_T2 = (int16_t)(data[3] << 8) | data[2];
    calib.dig_T3 = (int16_t)(data[5] << 8) | data[4];

    calib.dig_P1 = (uint16_t)(data[7] << 8) | data[6];
    calib.dig_P2 = (int16_t)(data[9] << 8) | data[8];
    calib.dig_P3 = (int16_t)(data[11] << 8) | data[10];
    calib.dig_P4 = (int16_t)(data[13] << 8) | data[12];
    calib.dig_P5 = (int16_t)(data[15] << 8) | data[14];
    calib.dig_P6 = (int16_t)(data[17] << 8) | data[16];
    calib.dig_P7 = (int16_t)(data[19] << 8) | data[18];
    calib.dig_P8 = (int16_t)(data[21] << 8) | data[20];
    calib.dig_P9 = (int16_t)(data[23] << 8) | data[22];

    calib.dig_H1 = data[23];
    calib.dig_H2 = (int16_t)(data[25] << 8) | data[24];
    calib.dig_H3 = data[26];
    calib.dig_H4 = (int16_t)((data[27] << 4) | (data[28] & 0x0F));
    calib.dig_H5 = (int16_t)(((data[29] & 0x0F) << 4) | (data[28] >> 4));
    calib.dig_H6 = (int8_t)data[30];
}

// t_fine carries fine temperature as global value
static int32_t t_fine;

// Returns temperature in DegC, resolution is 0.01 DegC. Output value of “5123” equals 51.23 DegC.
float bme280_read_temp_dC()
{
    uint16_t temp_raw = bme280_read_temp_raw();
    int32_t adc_T = ((int32_t)temp_raw) << 4;
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - (calib.dig_T1 << 1))) * (calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - (calib.dig_T1)) * ((adc_T >> 4) - (calib.dig_T1))) >> 12) * (calib.dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    float temp_dC = (float)T / 100.0;
    return temp_dC;
}

// Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format (24 integer bits and 8 fractional bits).
// Output value of “24674867” represents 24674867/256 = 96386.2 Pa = 963.862 hPa
float bme280_read_press_Pa()
{
    uint16_t press_raw = bme280_read_press_raw();
    int32_t adc_P = ((int32_t)press_raw) << 4;
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * calib.dig_P6;
    var2 = var2 + ((var1 * calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);
    var1 = ((var1 * var1 * calib.dig_P3) >> 8) + ((var1 * calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * (calib.dig_P1) >> 33;
    if (var1 == 0)
    {
        return 0; // avoid exception caused by division by zero
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((calib.dig_P7) << 4);
    float press_Pa = (float)p / 256.0;
    return press_Pa;
}

// Returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22 integer and 10 fractional bits).
// Output value of “47445” represents 47445/1024 = 46.333 %RH
float bme280_read_hum_pct()
{
    uint16_t hum_raw = bme280_read_hum_raw();
    int32_t adc_H = (int32_t)hum_raw;
    int32_t v_x1_u32r;
    v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - ((calib.dig_H4) << 20) - ((calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) * (((((((v_x1_u32r * (calib.dig_H6)) >> 10) * (((v_x1_u32r * (calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * (calib.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * (calib.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    uint32_t h = (uint32_t)(v_x1_u32r >> 12);
    float hum_pct = (float)h / 1024.0;
    return hum_pct;
}
