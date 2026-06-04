#include "sd-task.h"
#include "sd-driver.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "stdio.h"
#include "string.h"

/* ── Распиновка ──────────────────────────────────────────────────────────── */

#define SD_PIN_SCK  10
#define SD_PIN_MOSI 11
#define SD_PIN_MISO 12
#define SD_PIN_CS   13

/* ── HAL-реализация через Pico SDK ───────────────────────────────────────── */

static void rp2040_spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t size)
{
    spi_write_read_blocking(spi1, tx, rx, size);
}

static void rp2040_cs_set(bool level)
{
    gpio_put(SD_PIN_CS, level);
}

static void rp2040_delay_ms(uint32_t ms)
{
    sleep_ms(ms);
}

static const sd_hal_t sd_hal = {
    .spi_transfer = rp2040_spi_transfer,
    .cs_set       = rp2040_cs_set,
    .delay_ms     = rp2040_delay_ms,
};

static sd_t sd = {0};
static bool sd_ready = false;

/* ── Инициализация железа и карты ────────────────────────────────────────── */

void sd_task_init()
{
    /* Инициализация SPI1 на частоте 400 кГц для фазы init */
    spi_init(spi1, 400000);
    gpio_set_function(SD_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);
}

static sd_err_t ensure_init()
{
    if (sd_ready)
        return SD_OK;

    /* Инициализация на 400 кГц — максимум по спецификации SD */
    spi_set_baudrate(spi1, 400000);
    sd_err_t err = sd_init(&sd, &sd_hal);
    if (err != SD_OK)
        return err;

    /* Переходим на 10 МГц для рабочего режима */
    spi_set_baudrate(spi1, 10000000);
    sd_ready = true;
    return SD_OK;
}

sd_err_t sd_task_ensure_init(void) { return ensure_init(); }

const sd_t *sd_task_card(void) { return &sd; }

/* ── Тестовый паттерн ────────────────────────────────────────────────────── */

#define TEST_BLOCK_DEFAULT 2048

static void fill_test_pattern(uint8_t *buf, uint32_t block_num)
{
    /* Первые 4 байта — номер блока (little-endian) для идентификации */
    buf[0] = (block_num      ) & 0xFF;
    buf[1] = (block_num >>  8) & 0xFF;
    buf[2] = (block_num >> 16) & 0xFF;
    buf[3] = (block_num >> 24) & 0xFF;
    for (uint32_t i = 4; i < 512; i++)
        buf[i] = i & 0xFF;
}

/* ── Команды API ─────────────────────────────────────────────────────────── */

void sd_info_callback(const char *args)
{
    (void)args;
    sd_ready = false;  /* сброс, чтобы переинициализировать с нуля */

    spi_set_baudrate(spi1, 400000);
    sd_err_t err = sd_init_verbose(&sd, &sd_hal);
    if (err != SD_OK) {
        printf("sd_init error: %s\n", sd_err_str(err));
        return;
    }
    spi_set_baudrate(spi1, 10000000);
    sd_ready = true;

    printf("SD card type : %s\n", sd_type_str(sd.type));
    printf("SPI baudrate : 10 MHz (working)\n");
    printf("Block size   : 512 bytes\n");
}

void sd_test_callback(const char *args)
{
    uint32_t block_num = TEST_BLOCK_DEFAULT;
    sscanf(args, "%lu", &block_num);

    sd_err_t err = ensure_init();
    if (err != SD_OK) {
        printf("init error: %s\n", sd_err_str(err));
        return;
    }

    static uint8_t write_buf[512];
    static uint8_t read_buf[512];

    fill_test_pattern(write_buf, block_num);

    printf("Writing pattern to block %lu ... ", block_num);
    err = sd_write_block(&sd, block_num, write_buf);
    if (err != SD_OK) {
        printf("FAIL: %s\n", sd_err_str(err));
        return;
    }
    printf("OK\n");

    printf("Reading block %lu ... ", block_num);
    err = sd_read_block(&sd, block_num, read_buf);
    if (err != SD_OK) {
        printf("FAIL: %s\n", sd_err_str(err));
        return;
    }
    printf("OK\n");

    if (memcmp(write_buf, read_buf, 512) == 0) {
        printf("Verify: PASS — all 512 bytes match\n");
    } else {
        printf("Verify: FAIL — first mismatch at offset ");
        for (int i = 0; i < 512; i++) {
            if (write_buf[i] != read_buf[i]) {
                printf("%d (wrote 0x%02X, read 0x%02X)\n", i, write_buf[i], read_buf[i]);
                break;
            }
        }
    }
}

/* Посылает один сырой SPI-байт и возвращает принятый.
   Используем через HAL чтобы не дублировать код. */
static uint8_t raw_byte(uint8_t out)
{
    uint8_t in = 0;
    rp2040_spi_transfer(&out, &in, 1);
    return in;
}

/* Посылает 6-байтную SD-команду и читает 10 байт ответа подряд (без остановки).
   Распечатывает каждый принятый байт — для диагностики. */
static void raw_cmd_dump(uint8_t cmd, uint32_t arg, uint8_t crc, const char *label)
{
    printf("%s: send %02X %02X %02X %02X %02X %02X | recv: ",
           label,
           (uint8_t)(0x40 | cmd),
           (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
           (uint8_t)(arg >> 8),  (uint8_t)(arg),
           crc);

    raw_byte(0xFF);               /* пробуждающий байт */
    raw_byte(0x40 | cmd);
    raw_byte((arg >> 24) & 0xFF);
    raw_byte((arg >> 16) & 0xFF);
    raw_byte((arg >>  8) & 0xFF);
    raw_byte( arg        & 0xFF);
    raw_byte(crc);

    for (int i = 0; i < 10; i++)
        printf("%02X ", raw_byte(0xFF));
    printf("\n");
}

/* Команда глубокой диагностики: CMD0 → CMD8 → CMD58 → CMD41 без CMD55 →
   CMD55 → CMD41. Печатает сырые байты каждого ответа чтобы понять что
   именно отвечает карта. */
void sd_debug_callback(const char *args)
{
    (void)args;

    spi_set_baudrate(spi1, 400000);

    printf("=== SD raw bus diagnostic ===\n");

    /* 128 idle clocks */
    gpio_put(SD_PIN_CS, 1);
    for (int i = 0; i < 16; i++) raw_byte(0xFF);
    sleep_ms(10);

    /* CMD0 */
    gpio_put(SD_PIN_CS, 0);
    raw_cmd_dump(0, 0, 0x95, "CMD0 ");
    raw_byte(0xFF);
    gpio_put(SD_PIN_CS, 1); raw_byte(0xFF);

    /* CMD8 */
    gpio_put(SD_PIN_CS, 0);
    raw_cmd_dump(8, 0x000001AA, 0x87, "CMD8 ");
    raw_byte(0xFF);
    gpio_put(SD_PIN_CS, 1); raw_byte(0xFF);

    /* CMD58 — должен вернуть R3 (5 байт) в idle-состоянии */
    gpio_put(SD_PIN_CS, 0);
    raw_cmd_dump(58, 0, 0x01, "CMD58");
    raw_byte(0xFF);
    gpio_put(SD_PIN_CS, 1); raw_byte(0xFF);

    /* CMD41 БЕЗ CMD55 — ожидаем 0x05 (illegal command), если карта в норме */
    gpio_put(SD_PIN_CS, 0);
    raw_cmd_dump(41, 0x40000000, 0x01, "CMD41 (no CMD55, expect 0x05)");
    raw_byte(0xFF);
    gpio_put(SD_PIN_CS, 1); raw_byte(0xFF);

    /* CMD55 + CMD41 (ACMD41) — ожидаем 0x01 или 0x00 */
    gpio_put(SD_PIN_CS, 0);
    raw_cmd_dump(55, 0, 0x01, "CMD55");
    raw_cmd_dump(41, 0x40000000, 0x01, "CMD41 (after CMD55)");
    raw_byte(0xFF);
    gpio_put(SD_PIN_CS, 1); raw_byte(0xFF);

    printf("=== done ===\n");
}

void sd_write_block_callback(const char *args)
{
    uint32_t block_num = 0;
    int n = sscanf(args, "%lu", &block_num);
    if (n < 1) {
        printf("usage: sd_write_block <block_num>\n");
        return;
    }

    sd_err_t err = ensure_init();
    if (err != SD_OK) {
        printf("init error: %s\n", sd_err_str(err));
        return;
    }

    static uint8_t buf[512];
    fill_test_pattern(buf, block_num);

    err = sd_write_block(&sd, block_num, buf);
    printf("write block %lu: %s\n", block_num, sd_err_str(err));
}

void sd_read_block_callback(const char *args)
{
    uint32_t block_num = 0;
    int n = sscanf(args, "%lu", &block_num);
    if (n < 1) {
        printf("usage: sd_read_block <block_num>\n");
        return;
    }

    sd_err_t err = ensure_init();
    if (err != SD_OK) {
        printf("init error: %s\n", sd_err_str(err));
        return;
    }

    static uint8_t buf[512];
    err = sd_read_block(&sd, block_num, buf);
    if (err != SD_OK) {
        printf("read block %lu: %s\n", block_num, sd_err_str(err));
        return;
    }

    printf("Block %lu (512 bytes):\n", block_num);
    for (int row = 0; row < 512; row += 16) {
        printf("%03X: ", row);
        for (int col = 0; col < 16 && row + col < 512; col++)
            printf("%02X ", buf[row + col]);
        printf("\n");
    }
}
