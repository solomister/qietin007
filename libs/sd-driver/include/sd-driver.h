#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SD_BLOCK_SIZE 512

typedef struct {
    void (*spi_transfer)(const uint8_t *tx, uint8_t *rx, uint32_t size);
    void (*cs_set)(bool level);
    void (*delay_ms)(uint32_t ms);
} sd_hal_t;

typedef enum {
    SD_TYPE_UNKNOWN = 0,
    SD_TYPE_SDSC,
    SD_TYPE_SDHC,
} sd_type_t;

typedef struct {
    const sd_hal_t *hal;
    sd_type_t type;
} sd_t;

typedef enum {
    SD_OK = 0,
    SD_ERR_NO_CARD,
    SD_ERR_INIT,
    SD_ERR_TIMEOUT,
    SD_ERR_WRITE,
    SD_ERR_READ,
} sd_err_t;

sd_err_t sd_init(sd_t *sd, const sd_hal_t *hal);
sd_err_t sd_init_verbose(sd_t *sd, const sd_hal_t *hal);  /* то же, но с printf на каждом шаге */
sd_err_t sd_read_block(const sd_t *sd, uint32_t block_num, uint8_t *buf);
sd_err_t sd_write_block(const sd_t *sd, uint32_t block_num, const uint8_t *buf);

const char *sd_type_str(sd_type_t type);
const char *sd_err_str(sd_err_t err);
