#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#include "f_util.h"
#include "ff.h"
#include "hardware/spi.h"
#include "hw_config.h"

void list_dir(const char *path)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK)
    {
        printf("Cannot open dir '%s': %d\n", path, fr);
        return;
    }

    printf("\n=== Contents of %s ===\n", path);
    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
            break;

        if (fno.fattrib & AM_DIR)
        {
            printf("  [DIR]  %s\n", fno.fname);
        }
        else
        {
            printf("  %7lu  %s\n", fno.fsize, fno.fname);
        }
    }
    f_closedir(&dir);
    printf("=== End ===\n\n");
}

void read_file(const char *path)
{
    FIL file;
    FRESULT fr;
    char buf[256];
    UINT br;

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK)
    {
        printf("Cannot open file '%s': %d\n", path, fr);
        return;
    }

    printf("\n--- %s (%lu bytes) ---\n", path, f_size(&file));
    while (1)
    {
        fr = f_read(&file, buf, sizeof(buf) - 1, &br);
        if (fr != FR_OK || br == 0)
            break;
        buf[br] = '\0';
        printf("%s", buf);
    }
    printf("\n--- EOF ---\n\n");

    f_close(&file);
}

void write_test_file()
{
    FIL file;
    FRESULT fr;

    fr = f_open(&file, "0:/test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK)
    {
        printf("Cannot create test file: %d\n", fr);
        return;
    }

    f_printf(&file, "Hello from Pico!\n");
    f_printf(&file, "SD card is working.\n");
    f_close(&file);
    printf("Wrote test.txt\n");
}

#define SD_MISO 16
#define SD_MOSI 19
#define SD_SCK 18
#define SD_CS 17
#define SD_BAUDRATE 1000000

static spi_t spis[] = {
    {.hw_inst = spi0,
     .miso_gpio = SD_MISO,
     .mosi_gpio = SD_MOSI,
     .sck_gpio = SD_SCK,
     .baud_rate = SD_BAUDRATE}};

static sd_card_t sd_cards[] = {
    {.pcName = "0:",
     .spi = &spis[0],
     .ss_gpio = SD_CS,
     .use_card_detect = false}};

size_t sd_get_num()
{
    return 1;
}

sd_card_t *sd_get_by_num(size_t num)
{
    return num == 0 ? &sd_cards[0] : NULL;
}

size_t spi_get_num()
{
    return 1;
}

spi_t *spi_get_by_num(size_t num)
{
    return num == 0 ? &spis[0] : NULL;
}

int main()
{
    stdio_init_all();

    while (!stdio_usb_connected())
    {
        sleep_ms(100);
    }

    printf("Configuring GPIOs...\n");

    gpio_init(SD_MISO);
    gpio_init(SD_MOSI);
    gpio_init(SD_SCK);

    gpio_set_function(SD_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(SD_MISO);
    gpio_set_function(SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_SCK, GPIO_FUNC_SPI);

    gpio_init(SD_CS);
    gpio_set_dir(SD_CS, GPIO_OUT);
    gpio_put(SD_CS, 1); // CS high = неактивен

    printf("GPIOs configured. Trying SPI...\n");

    uint baud = spi_init(spi0, SD_BAUDRATE);
    printf("SPI initialized at %u Hz\n", baud);

    printf("=== SD Card Reader ===\n");

    // Монтируем карту
    FATFS fs;
    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK)
    {
        printf("SD mount FAILED: error %d\n", fr);
        printf("Check wiring and card format (FAT32)\n");
        while (1)
            sleep_ms(1000);
    }
    printf("SD mounted OK!\n");

    // Инфо о карте
    DWORD free_clust;
    FATFS *fs_ptr;
    fr = f_getfree("0:", &free_clust, &fs_ptr);
    if (fr == FR_OK)
    {
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

    while (1)
    {
        sleep_ms(1000);
    }
}
