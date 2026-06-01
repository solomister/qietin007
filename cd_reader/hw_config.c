#include "hw_config.h"

static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio = 18,
        .baud_rate = 400000
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spis[0],
        .ss_gpio = 17,
        .use_card_detect = false
    }
};

size_t sd_get_num() { return 1; }
sd_card_t *sd_get_by_num(size_t num) { return num == 0 ? &sd_cards[0] : NULL; }
size_t spi_get_num() { return 1; }
spi_t *spi_get_by_num(size_t num) { return num == 0 ? &spis[0] : NULL; }
