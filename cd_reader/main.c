#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "f_util.h"
#include "ff.h"
#include "hardware/spi.h"

void list_dir(const char *path) {
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        printf("Cannot open dir '%s': %d\n", path, fr);
        return;
    }

    printf("\n=== Contents of %s ===\n", path);
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        if (fno.fattrib & AM_DIR) {
            printf("  [DIR]  %s\n", fno.fname);
        } else {
            printf("  %7lu  %s\n", fno.fsize, fno.fname);
        }
    }
    f_closedir(&dir);
    printf("=== End ===\n\n");
}

void read_file(const char *path) {
    FIL file;
    FRESULT fr;
    char buf[256];
    UINT br;

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("Cannot open file '%s': %d\n", path, fr);
        return;
    }

    printf("\n--- %s (%lu bytes) ---\n", path, f_size(&file));
    while (1) {
        fr = f_read(&file, buf, sizeof(buf) - 1, &br);
        if (fr != FR_OK || br == 0) break;
        buf[br] = '\0';
        printf("%s", buf);
    }
    printf("\n--- EOF ---\n\n");

    f_close(&file);
}

void write_test_file() {
    FIL file;
    FRESULT fr;

    fr = f_open(&file, "0:/test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("Cannot create test file: %d\n", fr);
        return;
    }

    f_printf(&file, "Hello from Pico!\n");
    f_printf(&file, "SD card is working.\n");
    f_close(&file);
    printf("Wrote test.txt\n");
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    uint baud = spi_init(spi0, 1000000);  // 1 МГц для начала
    printf("SPI initialized at %u Hz\n", baud);

    gpio_set_function(16, GPIO_FUNC_SPI);  // MISO
    gpio_set_function(18, GPIO_FUNC_SPI);  // SCK
    gpio_set_function(19, GPIO_FUNC_SPI);  // MOSI

    gpio_init(17);                          // CS — обычный GPIO
    gpio_set_dir(17, GPIO_OUT);
    gpio_put(17, 1);                        // CS high = неактивен

    printf("Pins configured. Trying SD...\n");

    printf("=== SD Card Reader ===\n");

    // Монтируем карту
    FATFS fs;
    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) {
        printf("SD mount FAILED: error %d\n", fr);
        printf("Check wiring and card format (FAT32)\n");
        while (1) sleep_ms(1000);
    }
    printf("SD mounted OK!\n");

    // Инфо о карте
    DWORD free_clust;
    FATFS *fs_ptr;
    fr = f_getfree("0:", &free_clust, &fs_ptr);
    if (fr == FR_OK) {
        uint32_t total_mb = (fs_ptr->n_fatent - 2) * fs_ptr->csize / 2048;
        uint32_t free_mb = free_clust * fs_ptr->csize / 2048;
        printf("Total: %lu MB, Free: %lu MB\n", total_mb, free_mb);
    }

    // Показываем содержимое корня
    list_dir("0:/");

    // Пишем тестовый файл
    write_test_file();

    // Читаем его обратно
    read_file("0:/test.txt");

    // Показываем обновлённое содержимое
    list_dir("0:/");

    // Размонтируем
    f_unmount("0:");
    printf("SD unmounted. Done!\n");

    while (1) {
        sleep_ms(1000);
    }
}