#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define ADC_PIN 26
#define ADC_CHANNEL 0

int main() {
    stdio_init_all();
    sleep_ms(2000);  // ждём пока USB подключится

    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_CHANNEL);

    printf("=== Photoresistor ADC Reader ===\n");
    printf("raw (0-4095) | voltage (V)\n");
    printf("-----------------------------\n");

    while (1) {
        uint16_t raw = adc_read();              // 12 бит: 0–4095
        float voltage = raw * 3.3f / 4095.0f;   // перевод в вольты

        printf("raw = %4u | voltage = %.3f V\n", raw, voltage);
        sleep_ms(50);
    }
}