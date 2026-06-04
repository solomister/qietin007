#include "pico/stdlib.h"

#include "stdio-task/stdio-task.h"
#include "led-task/led-task.h"
#include "sd-task/sd-task.h"
#include "wav-task/wav-task.h"
#include "rec-task/rec-task.h"
#include "protocol-task.h"

#include "stdio.h"

#define DEVICE_NAME "sd-recorder"
#define DEVICE_VRSN "v0.3.0-stage3"

void version_callback(const char *args) { printf("device: '%s', version: %s\n", DEVICE_NAME, DEVICE_VRSN); }
void help_callback(const char *args)    { protocol_task_print_help(); }

void led_off_callback(const char *args)   { led_task_set_state(LED_STATE_OFF); }
void led_on_callback(const char *args)    { led_task_set_state(LED_STATE_ON); }
void led_blink_callback(const char *args) { led_task_set_state(LED_STATE_BLINK); }

api_t device_api[] = {
    {"version",        version_callback,        "get device name and firmware version"},
    {"help",           help_callback,           "print commands description"},

    {"off",            led_off_callback,        "turn off LED"},
    {"on",             led_on_callback,         "turn on LED"},
    {"blink",          led_blink_callback,      "blink LED"},

    /* ── SD low-level (этап 1) ────────────────────────────────────────────── */
    {"sd_info",        sd_info_callback,        "init SD card and print info (verbose)"},
    {"sd_debug",       sd_debug_callback,       "raw SPI bus diagnostic: dump responses to CMD0/8/58/41/ACMD41"},
    {"sd_test",        sd_test_callback,        "sd_test [block] — write/read/verify pattern (default block=2048)"},
    {"sd_write_block", sd_write_block_callback, "sd_write_block <block> — write test pattern to block"},
    {"sd_read_block",  sd_read_block_callback,  "sd_read_block <block>  — read and hex-dump block"},

    /* ── FAT32 + WAV (этап 2) ─────────────────────────────────────────────── */
    {"wav_sine",       wav_sine_callback,       "wav_sine <freq_hz> <seconds> — generate sine WAV file"},
    {"sd_ls",          sd_ls_callback,          "list WAV files on SD card"},
    {"sd_download",    sd_download_callback,    "sd_download <name.wav> — transfer file via base64 serial"},

    /* ── ADC запись (этап 3) ──────────────────────────────────────────────── */
    {"rec_start",      rec_start_callback,      "rec_start [seconds] — record from GPIO26/ADC0 (0 or omit = until rec_stop)"},
    {"rec_stop",       rec_stop_callback,       "rec_stop — stop recording and save WAV file"},

    {NULL, NULL, NULL},
};

int main()
{
    stdio_init_all();

    sd_task_init();
    wav_task_init();
    rec_task_init();
    stdio_task_init();
    protocol_task_init(device_api);
    led_task_init();

    char *command;
    while (1) {
        command = stdio_task_handle();
        protocol_task_handle(command);
        led_task_handle();
    }
}
