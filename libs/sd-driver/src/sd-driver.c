#include "sd-driver.h"
#include <string.h>
#include <stdio.h>

/* ── Команды SD/SPI ──────────────────────────────────────────────────────── */

#define CMD0    0   /* GO_IDLE_STATE    */
#define CMD8    8   /* SEND_IF_COND     */
#define CMD16   16  /* SET_BLOCKLEN     */
#define CMD17   17  /* READ_SINGLE_BLOCK */
#define CMD24   24  /* WRITE_BLOCK      */
#define CMD55   55  /* APP_CMD          */
#define CMD58   58  /* READ_OCR         */
#define ACMD41  41  /* SD_SEND_OP_COND  */

/* R1 флаги */
#define R1_IDLE_STATE   0x01
#define R1_ILLEGAL_CMD  0x04

/* Токены данных */
#define DATA_TOKEN_READ  0xFE
#define DATA_TOKEN_WRITE 0xFE
#define DATA_RESP_MASK   0x1F
#define DATA_RESP_OK     0x05

/* Таймауты */
#define TIMEOUT_INIT_MS  2000
#define TIMEOUT_READ_MS   200
#define TIMEOUT_WRITE_MS  10000  /* SD-карта может стирать NAND-блок до ~3 сек */

/* ── Вспомогательные функции ─────────────────────────────────────────────── */

static uint8_t spi_byte(const sd_t *sd, uint8_t out)
{
    uint8_t in = 0;
    sd->hal->spi_transfer(&out, &in, 1);
    return in;
}

static void spi_skip(const sd_t *sd, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        spi_byte(sd, 0xFF);
}

static uint8_t sd_send_cmd(const sd_t *sd, uint8_t cmd, uint32_t arg)
{
    /* Пробуждающий байт: активирует DO (MISO) карты после опускания CS */
    spi_byte(sd, 0xFF);

    /* CRC нужен только для CMD0 и CMD8 */
    uint8_t crc = 0x01;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;

    spi_byte(sd, 0x40 | cmd);
    spi_byte(sd, (arg >> 24) & 0xFF);
    spi_byte(sd, (arg >> 16) & 0xFF);
    spi_byte(sd, (arg >>  8) & 0xFF);
    spi_byte(sd, (arg      ) & 0xFF);
    spi_byte(sd, crc);

    /* NCR: ждём начало ответа (бит 7 = 0), до 8 байт */
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 8; i++) {
        r1 = spi_byte(sd, 0xFF);
        if (!(r1 & 0x80))
            break;
    }
    return r1;
}

/* ── Инициализация ───────────────────────────────────────────────────────── */

sd_err_t sd_init(sd_t *sd, const sd_hal_t *hal)
{
    sd->hal  = hal;
    sd->type = SD_TYPE_UNKNOWN;

    /* 1. Подача питания — ждём 100 мс чтобы карта стабилизировалась */
    hal->delay_ms(100);

    /* 2. ≥128 тактов с CS=HIGH → карта гарантированно переходит в SPI mode */
    hal->cs_set(true);
    spi_skip(sd, 16);

    /* 3. CMD0 — программный сброс. Повторяем до 10 раз: некоторые карты
       не отвечают с первого раза, особенно сразу после подачи питания */
    uint8_t r1 = 0xFF;
    for (int attempt = 0; attempt < 10; attempt++) {
        hal->cs_set(false);
        r1 = sd_send_cmd(sd, CMD0, 0);
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);
        if (r1 == R1_IDLE_STATE)
            break;
        hal->delay_ms(10);
    }

    if (r1 != R1_IDLE_STATE)
        return SD_ERR_NO_CARD;

    /* 4. CMD8 — проверка версии (нужна для SDHC).
       Если CMD8 принят (R1=0x01) — это v2 карта (SDHC/SDXC capable).
       Echo-байты читаем и сбрасываем, но тип определяем по факту принятия CMD8. */
    hal->cs_set(false);
    r1 = sd_send_cmd(sd, CMD8, 0x000001AA);

    bool is_v2 = false;
    if (r1 == R1_IDLE_STATE) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++)
            r7[i] = spi_byte(sd, 0xFF);
        (void)r7;
        is_v2 = true; /* CMD8 принят → карта v2 */
    }
    spi_skip(sd, 1);
    hal->cs_set(true);
    spi_skip(sd, 1);

    /* 5. ACMD41 — инициализация контроллера карты */
    uint32_t hcs_arg = is_v2 ? 0x40000000 : 0;
    uint32_t elapsed = 0;
    do {
        /* CMD55 + CMD41 в одной CS-транзакции: карта «забывает» APP_CMD если CS поднять между ними */
        hal->cs_set(false);
        sd_send_cmd(sd, CMD55, 0);
        r1 = sd_send_cmd(sd, ACMD41, hcs_arg);
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);

        if (r1 == 0x00)
            break;

        hal->delay_ms(10);
        elapsed += 10;
    } while (elapsed < TIMEOUT_INIT_MS);

    if (r1 != 0x00)
        return SD_ERR_INIT;

    /* 6. CMD58 — читаем OCR, определяем SDHC по биту CCS */
    sd->type = SD_TYPE_SDSC;
    if (is_v2) {
        hal->cs_set(false);
        r1 = sd_send_cmd(sd, CMD58, 0);
        if (r1 == 0x00) {
            uint8_t ocr[4];
            for (int i = 0; i < 4; i++)
                ocr[i] = spi_byte(sd, 0xFF);
            if (ocr[0] & 0x40)
                sd->type = SD_TYPE_SDHC;
        }
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);
    }

    /* 7. SDSC: задать размер блока 512 байт */
    if (sd->type == SD_TYPE_SDSC) {
        hal->cs_set(false);
        r1 = sd_send_cmd(sd, CMD16, SD_BLOCK_SIZE);
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);
        if (r1 != 0x00)
            return SD_ERR_INIT;
    }

    return SD_OK;
}

/* ── Чтение блока ────────────────────────────────────────────────────────── */

sd_err_t sd_read_block(const sd_t *sd, uint32_t block_num, uint8_t *buf)
{
    uint32_t addr = (sd->type == SD_TYPE_SDHC) ? block_num : block_num * SD_BLOCK_SIZE;

    sd->hal->cs_set(false);

    uint8_t r1 = sd_send_cmd(sd, CMD17, addr);
    if (r1 != 0x00) {
        spi_skip(sd, 1);
        sd->hal->cs_set(true);
        return SD_ERR_READ;
    }

    /* Ждём токен данных 0xFE.
       Цикл побайтового опроса + реальная задержка 1 мс между итерациями —
       таймаут корректен при любой частоте SPI (спецификация допускает до 100 мс). */
    uint8_t token = 0xFF;
    bool token_found = false;
    for (uint32_t t = 0; t < TIMEOUT_READ_MS && !token_found; t++) {
        for (int j = 0; j < 1000; j++) {
            token = spi_byte(sd, 0xFF);
            if (token != 0xFF) { token_found = true; break; }
        }
        if (!token_found)
            sd->hal->delay_ms(1);
    }

    if (token != DATA_TOKEN_READ) {
        spi_skip(sd, 1);
        sd->hal->cs_set(true);
        return (token == 0xFF) ? SD_ERR_TIMEOUT : SD_ERR_READ;
    }

    /* Читаем 512 байт */
    static const uint8_t tx_ff[SD_BLOCK_SIZE];
    /* tx_ff заполнен нулями при старте, но нам нужны 0xFF — используем поэлементно */
    for (uint32_t i = 0; i < SD_BLOCK_SIZE; i++)
        buf[i] = spi_byte(sd, 0xFF);

    /* 2 байта CRC — игнорируем */
    spi_byte(sd, 0xFF);
    spi_byte(sd, 0xFF);

    spi_skip(sd, 1);
    sd->hal->cs_set(true);
    spi_skip(sd, 1);

    return SD_OK;
}

/* ── Запись блока ────────────────────────────────────────────────────────── */

sd_err_t sd_write_block(const sd_t *sd, uint32_t block_num, const uint8_t *buf)
{
    uint32_t addr = (sd->type == SD_TYPE_SDHC) ? block_num : block_num * SD_BLOCK_SIZE;

    sd->hal->cs_set(false);

    uint8_t r1 = sd_send_cmd(sd, CMD24, addr);
    if (r1 != 0x00) {
        spi_skip(sd, 1);
        sd->hal->cs_set(true);
        return SD_ERR_WRITE;
    }

    /* Пауза + токен начала данных */
    spi_byte(sd, 0xFF);
    spi_byte(sd, DATA_TOKEN_WRITE);

    /* Данные */
    for (uint32_t i = 0; i < SD_BLOCK_SIZE; i++)
        spi_byte(sd, buf[i]);

    /* CRC-заглушка */
    spi_byte(sd, 0xFF);
    spi_byte(sd, 0xFF);

    /* Data response token */
    uint8_t resp = spi_byte(sd, 0xFF);
    if ((resp & DATA_RESP_MASK) != DATA_RESP_OK) {
        spi_skip(sd, 1);
        sd->hal->cs_set(true);
        return SD_ERR_WRITE;
    }

    /* Ждём пока карта занята (MISO = 0x00) */
    uint32_t elapsed = 0;
    while (spi_byte(sd, 0xFF) == 0x00) {
        sd->hal->delay_ms(1);
        elapsed++;
        if (elapsed > TIMEOUT_WRITE_MS) {
            sd->hal->cs_set(true);
            return SD_ERR_TIMEOUT;
        }
    }

    spi_skip(sd, 1);
    sd->hal->cs_set(true);
    spi_skip(sd, 1);

    return SD_OK;
}

/* ── Диагностическая инициализация (с printf на каждом шаге) ────────────── */

sd_err_t sd_init_verbose(sd_t *sd, const sd_hal_t *hal)
{
    sd->hal  = hal;
    sd->type = SD_TYPE_UNKNOWN;

    printf("[sd] power-up delay 100ms\n");
    hal->delay_ms(100);

    printf("[sd] sending 128 idle clocks (CS=HIGH)\n");
    hal->cs_set(true);
    spi_skip(sd, 16);

    /* CMD0 с retry */
    uint8_t r1 = 0xFF;
    for (int attempt = 0; attempt < 10; attempt++) {
        hal->cs_set(false);
        r1 = sd_send_cmd(sd, CMD0, 0);
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);
        printf("[sd] CMD0 attempt %d: R1=0x%02X\n", attempt, r1);
        if (r1 == R1_IDLE_STATE)
            break;
        hal->delay_ms(10);
    }
    if (r1 != R1_IDLE_STATE) {
        printf("[sd] CMD0 failed — no card or SPI wiring issue\n");
        return SD_ERR_NO_CARD;
    }
    printf("[sd] CMD0 OK\n");

    /* CMD8 */
    hal->cs_set(false);
    r1 = sd_send_cmd(sd, CMD8, 0x000001AA);
    bool is_v2 = false;
    if (r1 == R1_IDLE_STATE) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++)
            r7[i] = spi_byte(sd, 0xFF);
        printf("[sd] CMD8 R1=0x%02X, R7=%02X %02X %02X %02X\n", r1, r7[0], r7[1], r7[2], r7[3]);
        is_v2 = true;
    } else {
        printf("[sd] CMD8 R1=0x%02X (0x05=SDSC v1, 0x01=SDHC)\n", r1);
    }
    spi_skip(sd, 1);
    hal->cs_set(true);
    spi_skip(sd, 1);
    printf("[sd] card version: %s\n", is_v2 ? "v2 (SDHC capable)" : "v1 (SDSC)");

    /* ACMD41 */
    uint32_t hcs_arg = is_v2 ? 0x40000000 : 0;
    uint32_t elapsed = 0;
    printf("[sd] starting ACMD41 loop (timeout 5000 ms)...\n");
    do {
        /* CMD55 + CMD41 в одной CS-транзакции */
        hal->cs_set(false);
        uint8_t r_cmd55 = sd_send_cmd(sd, CMD55, 0);
        r1 = sd_send_cmd(sd, ACMD41, hcs_arg);
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);

        if (elapsed % 200 == 0)
            printf("[sd] ACMD41 t=%lums: CMD55 R1=0x%02X, CMD41 R1=0x%02X\n",
                   (unsigned long)elapsed, r_cmd55, r1);

        if (r1 == 0x00)
            break;

        hal->delay_ms(10);
        elapsed += 10;
    } while (elapsed < 5000);

    printf("[sd] ACMD41 result: R1=0x%02X after %lums\n", r1, (unsigned long)elapsed);
    if (r1 != 0x00)
        return SD_ERR_INIT;

    /* CMD58 */
    sd->type = SD_TYPE_SDSC;
    if (is_v2) {
        hal->cs_set(false);
        r1 = sd_send_cmd(sd, CMD58, 0);
        uint8_t ocr[4] = {0};
        if (r1 == 0x00) {
            for (int i = 0; i < 4; i++)
                ocr[i] = spi_byte(sd, 0xFF);
            if (ocr[0] & 0x40)
                sd->type = SD_TYPE_SDHC;
        }
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);
        printf("[sd] CMD58 R1=0x%02X OCR=%02X %02X %02X %02X → %s\n",
               r1, ocr[0], ocr[1], ocr[2], ocr[3], sd_type_str(sd->type));
    }

    if (sd->type == SD_TYPE_SDSC) {
        hal->cs_set(false);
        r1 = sd_send_cmd(sd, CMD16, SD_BLOCK_SIZE);
        spi_skip(sd, 1);
        hal->cs_set(true);
        spi_skip(sd, 1);
        printf("[sd] CMD16 (SET_BLOCKLEN) R1=0x%02X\n", r1);
        if (r1 != 0x00)
            return SD_ERR_INIT;
    }

    printf("[sd] init complete: %s\n", sd_type_str(sd->type));
    return SD_OK;
}

/* ── Строковые описания ──────────────────────────────────────────────────── */

const char *sd_type_str(sd_type_t type)
{
    switch (type) {
    case SD_TYPE_SDSC: return "SDSC";
    case SD_TYPE_SDHC: return "SDHC/SDXC";
    default:           return "UNKNOWN";
    }
}

const char *sd_err_str(sd_err_t err)
{
    switch (err) {
    case SD_OK:          return "OK";
    case SD_ERR_NO_CARD: return "no card / CMD0 failed";
    case SD_ERR_INIT:    return "init failed (ACMD41 / CMD16)";
    case SD_ERR_TIMEOUT: return "timeout";
    case SD_ERR_WRITE:   return "write error";
    case SD_ERR_READ:    return "read error";
    default:             return "unknown error";
    }
}
