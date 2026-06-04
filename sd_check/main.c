#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT  spi0
#define PIN_MISO  16
#define PIN_SCK   18
#define PIN_MOSI  19
#define PIN_CS    17

// Тестируемый блок. 0x20000 = 64 МБ от начала — в области данных любой ≥128 МБ карты.
#define TEST_BLOCK  0x20000u
#define BLOCK_SIZE  512

// ── SPI helpers ──────────────────────────────────────────────────────────────

static inline void cs_low(void)  { gpio_put(PIN_CS, 0); }
static inline void cs_high(void) { gpio_put(PIN_CS, 1); }

static uint8_t spi_byte(uint8_t tx) {
    uint8_t rx;
    spi_write_read_blocking(SPI_PORT, &tx, &rx, 1);
    return rx;
}

// ── SD command helpers ────────────────────────────────────────────────────────

// Ждёт пока карта отпустит MISO (0xFF = готова к следующей команде)
static bool sd_wait_ready(void) {
    for (int i = 0; i < 10000; i++) {
        if (spi_byte(0xFF) == 0xFF) return true;
    }
    return false;
}

static void sd_send_cmd_frame(uint8_t cmd, uint32_t arg, uint8_t crc) {
    spi_byte(0x40 | cmd);
    spi_byte((arg >> 24) & 0xFF);
    spi_byte((arg >> 16) & 0xFF);
    spi_byte((arg >>  8) & 0xFF);
    spi_byte( arg        & 0xFF);
    spi_byte(crc);
}

// Ждёт первый не-0xFF байт (R1)
static uint8_t sd_wait_r1(void) {
    for (int i = 0; i < 100; i++) {
        uint8_t b = spi_byte(0xFF);
        if (b != 0xFF) return b;
    }
    return 0xFF;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    cs_low();
    if (!sd_wait_ready()) printf("  [warn] cmd%u: not ready\n", cmd);
    sd_send_cmd_frame(cmd, arg, crc);
    uint8_t r = sd_wait_r1();
    cs_high();
    spi_byte(0xFF);
    return r;
}

static uint8_t sd_cmd8(void) {
    cs_low();
    if (!sd_wait_ready()) printf("  [warn] cmd8: not ready\n");
    sd_send_cmd_frame(8, 0x000001AA, 0x87);
    uint8_t r = sd_wait_r1();
    if (r == 0x01) {
        uint8_t v[4];
        for (int i = 0; i < 4; i++) v[i] = spi_byte(0xFF);
        printf("  CMD8 R7: %02X %02X %02X %02X\n", v[0], v[1], v[2], v[3]);
    }
    cs_high();
    spi_byte(0xFF);
    return r;
}

static uint8_t sd_cmd58(bool *is_hc) {
    cs_low();
    if (!sd_wait_ready()) printf("  [warn] cmd58: not ready\n");
    sd_send_cmd_frame(58, 0, 0xFF);
    uint8_t r = sd_wait_r1();
    if (r == 0x00) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = spi_byte(0xFF);
        printf("  OCR: %02X %02X %02X %02X\n", ocr[0], ocr[1], ocr[2], ocr[3]);
        *is_hc = (ocr[0] & 0x40) != 0;
    }
    cs_high();
    spi_byte(0xFF);
    return r;
}

// verbose=true → печатает CMD55 и CMD41 отдельно
static uint8_t sd_acmd41(bool verbose) {
    uint8_t r55 = sd_cmd(55, 0, 0xFF);
    if (verbose) printf("  [CMD55→%02X", r55);
    if (r55 > 0x01) {
        if (verbose) printf(" —abort]\n");
        return r55;
    }
    uint8_t r41 = sd_cmd(41, 0x40000000, 0xFF);
    if (verbose) printf(" CMD41→%02X]\n", r41);
    return r41;
}

// CMD13: SEND_STATUS → R2 (2 байта); расшифровывает и печатает причину ошибки
static void sd_send_status(void) {
    cs_low();
    spi_byte(0xFF);
    sd_send_cmd_frame(13, 0, 0xFF);
    uint8_t r1 = sd_wait_r1();
    uint8_t r2 = spi_byte(0xFF);
    cs_high();
    spi_byte(0xFF);
    printf("  CMD13: R1=0x%02X  R2=0x%02X\n", r1, r2);
    if (r2 & 0x80) printf("    OUT_OF_RANGE / CSD_OVERWRITE\n");
    if (r2 & 0x40) printf("    ADDRESS_ERROR\n");
    if (r2 & 0x20) printf("    BLOCK_LEN_ERROR\n");
    if (r2 & 0x10) printf("    ERASE_SEQ_ERROR\n");
    if (r2 & 0x08) printf("    ERASE_PARAM\n");
    if (r2 & 0x04) printf("    WP_VIOLATION (карта защищена от записи!)\n");
    if (r2 & 0x02) printf("    CARD_IS_LOCKED\n");
    if (r2 & 0x01) printf("    LOCK_UNLOCK_FAILED\n");
    if (r2 == 0x00) printf("    Ошибок нет\n");
}

// ── Полная инициализация SD ───────────────────────────────────────────────────

static bool sd_init(bool *is_hc) {
    *is_hc = false;

    cs_high();
    for (int i = 0; i < 10; i++) spi_byte(0xFF);

    printf("CMD0... ");
    uint8_t r = sd_cmd(0, 0, 0x95);
    printf("0x%02X", r);
    if (r == 0x01) printf(" (idle OK)\n");
    else           { printf(" — ожидали 0x01!\n"); return false; }

    printf("CMD8... ");
    r = sd_cmd8();
    printf("0x%02X", r);
    bool v2 = (r == 0x01);
    if (v2) printf(" (SD v2)\n"); else printf(" (SD v1 / MMC)\n");

    printf("ACMD41 (первые 5 итераций verbose):\n");
    int ff_streak = 0;
    for (int i = 0; i < 2000; i++) {
        bool verbose = (i < 5);
        if (verbose) printf("  #%d", i);
        r = sd_acmd41(verbose);
        if (r == 0x00) break;
        if (r == 0x01) {
            ff_streak = 0;
        } else if (r == 0xFF) {
            ff_streak++;
            if (ff_streak >= 50) { printf("  ACMD41: 50 подряд 0xFF — карта не отвечает\n"); return false; }
        } else {
            printf("  ACMD41 error: 0x%02X\n", r);
            return false;
        }
        if (i >= 5 && i % 200 == 0) printf(".");
        sleep_ms(1);
    }
    printf("ACMD41 → 0x%02X", r);
    if (r == 0x00) printf(" (ready)\n"); else { printf(" — timeout!\n"); return false; }

    if (v2) {
        printf("CMD58... ");
        r = sd_cmd58(is_hc);
        printf("0x%02X (OCR говорит: %s)\n", r, *is_hc ? "SDHC/SDXC" : "SDSC v2");

        // Некоторые карты ошибочно сообщают CCS=0 в OCR, хотя являются SDHC.
        // Признак: CMD8 прошёл, карта ≥2 ГБ — почти наверняка SDHC.
        // Принудительно включаем блочную адресацию.
        if (!*is_hc) {
            *is_hc = true;
            printf("  OCR CCS=0, но карта v2 — принудительно SDHC (блочная адресация)\n");
        }
    }

    // 4 МГц вместо 10 — безопаснее на макетной плате
    uint32_t baud = spi_set_baudrate(SPI_PORT, 4000000);
    printf("SPI → %lu Hz\n", (unsigned long)baud);

    r = sd_cmd(16, 512, 0xFF);
    printf("CMD16: 0x%02X\n", r);

    return true;
}

// ── Чтение блока ──────────────────────────────────────────────────────────────

static bool sd_read_block(uint32_t block, bool is_hc, uint8_t *buf) {
    uint32_t addr = is_hc ? block : block * BLOCK_SIZE;

    cs_low();
    spi_byte(0xFF);
    sd_send_cmd_frame(17, addr, 0xFF);

    uint8_t r = sd_wait_r1();
    if (r != 0x00) {
        cs_high(); spi_byte(0xFF);
        printf("CMD17 R1=0x%02X\n", r);
        return false;
    }

    uint8_t tok = 0xFF;
    for (int i = 0; i < 10000; i++) {
        tok = spi_byte(0xFF);
        if (tok != 0xFF) break;
    }
    if (tok != 0xFE) {
        cs_high(); spi_byte(0xFF);
        printf("Нет токена данных: 0x%02X\n", tok);
        return false;
    }

    spi_read_blocking(SPI_PORT, 0xFF, buf, BLOCK_SIZE);
    spi_byte(0xFF); spi_byte(0xFF);

    cs_high();
    spi_byte(0xFF);
    return true;
}

// ── Запись блока ─────────────────────────────────────────────────────────────

static bool sd_write_block(uint32_t block, bool is_hc, const uint8_t *buf) {
    uint32_t addr = is_hc ? block : block * BLOCK_SIZE;

    cs_low();
    spi_byte(0xFF);
    sd_send_cmd_frame(24, addr, 0xFF);

    uint8_t r = sd_wait_r1();
    if (r != 0x00) {
        cs_high(); spi_byte(0xFF);
        printf("  CMD24 R1=0x%02X\n", r);
        return false;
    }

    spi_byte(0xFF);   // пауза перед токеном
    spi_byte(0xFE);   // токен начала данных

    spi_write_blocking(SPI_PORT, buf, BLOCK_SIZE);

    spi_byte(0xFF); spi_byte(0xFF); // dummy CRC

    // Читаем токен ответа записи — печатаем ВСЕ байты до первого не-0xFF
    printf("  Байты ответа записи: ");
    uint8_t resp = 0xFF;
    for (int i = 0; i < 16; i++) {
        uint8_t b = spi_byte(0xFF);
        printf("[%d]%02X ", i, b);
        if (b != 0xFF) {
            resp = b;
            break;
        }
    }
    printf("\n");

    if ((resp & 0x1F) != 0x05) {
        const char *meaning = "?";
        switch (resp & 0x1F) {
            case 0x0B: meaning = "CRC error"; break;
            case 0x0D: meaning = "write error"; break;
        }
        printf("  Ответ: 0x%02X → %s (ожидали 0xX5 = accepted)\n", resp, meaning);
        cs_high(); spi_byte(0xFF);
        return false;
    }

    // Ждём пока карта программирует (MISO=0 = занята, 0xFF = готово)
    uint32_t busy_count = 0;
    bool done = false;
    for (uint32_t i = 0; i < 500000; i++) {
        if (spi_byte(0xFF) != 0x00) { busy_count = i; done = true; break; }
    }
    cs_high();
    spi_byte(0xFF);

    // Типичное время программирования SD flash: 10–100 мс
    // При 4 МГц SPI: 2 мкс/итерация → 5000-50000 итераций для нормальной записи
    // Если busy_count == 0 → карта не вошла в режим занятости (подозрительно!)
    printf("  Busy итераций: %lu (≈%lu мс при 4 МГц)\n",
           (unsigned long)busy_count,
           (unsigned long)(busy_count * 2 / 1000));

    if (!done) { printf("  Write timeout\n"); return false; }
    return true;
}

// ── main ──────────────────────────────────────────────────────────────────────

static uint8_t write_buf[BLOCK_SIZE];
static uint8_t read_buf[BLOCK_SIZE];

int main() {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(500);

    printf("\n=== SD raw block write/read test ===\n\n");

    spi_init(SPI_PORT, 400000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_MISO);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    bool is_hc;
    if (!sd_init(&is_hc)) {
        printf("\n>>> SD init FAILED <<<\n");
        while (1) sleep_ms(1000);
    }
    printf("\nКарта (%s), блок 0x%lX\n\n", is_hc ? "SDHC" : "SDSC", (unsigned long)TEST_BLOCK);

    // ── 1. Читаем блок ДО записи (проверка чтения) ──────────────────────────
    printf("--- Чтение блока (до записи) ---\n");
    if (sd_read_block(TEST_BLOCK, is_hc, read_buf)) {
        printf("Чтение OK. Первые 16 байт: ");
        for (int i = 0; i < 16; i++) printf("%02X ", read_buf[i]);
        printf("\n\n");
    } else {
        printf(">>> ЧТЕНИЕ ПРОВАЛИЛОСЬ — дальнейший тест бессмысленен <<<\n");
        while (1) sleep_ms(1000);
    }

    // ── 2. Записываем тестовый паттерн ──────────────────────────────────────
    for (int i = 0; i < BLOCK_SIZE; i++) write_buf[i] = (uint8_t)(i & 0xFF);
    write_buf[0]   = 0xAA;
    write_buf[1]   = 0xBB;
    write_buf[510] = 0xCC;
    write_buf[511] = 0xDD;

    printf("--- Запись в блок 0x%lX ---\n", (unsigned long)TEST_BLOCK);
    if (!sd_write_block(TEST_BLOCK, is_hc, write_buf)) {
        printf(">>> ЗАПИСЬ ПРОВАЛИЛАСЬ <<<\n");
        printf("Запрашиваем статус карты (CMD13):\n");
        sd_send_status();
        printf("\nПодсказки:\n");
        printf("  • Проверь положение tab защиты от записи на карте\n");
        printf("  • Если OCR показал SDSC, но карта ≥4 ГБ — это SDHC! Поменяй is_hc вручную.\n");
        while (1) sleep_ms(1000);
    }
    printf("Запись OK\n");
    printf("CMD13 статус после записи:\n");
    sd_send_status();
    printf("\n");

    // ── 3. Читаем обратно и сравниваем ──────────────────────────────────────
    printf("--- Чтение блока (после записи) ---\n");
    if (!sd_read_block(TEST_BLOCK, is_hc, read_buf)) {
        printf(">>> ЧТЕНИЕ ПРОВАЛИЛОСЬ <<<\n");
        while (1) sleep_ms(1000);
    }

    int mismatches = 0;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (write_buf[i] != read_buf[i]) {
            if (mismatches < 8)
                printf("  [%3d] записали 0x%02X, прочитали 0x%02X\n",
                       i, write_buf[i], read_buf[i]);
            mismatches++;
        }
    }

    if (mismatches == 0)
        printf("\n>>> УСПЕХ: все %d байт совпали! <<<\n", BLOCK_SIZE);
    else
        printf("\n>>> ОШИБКА: %d несовпадений <<<\n", mismatches);

    while (1) sleep_ms(1000);
}
