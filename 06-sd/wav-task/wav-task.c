#include "wav-task.h"

#include "ff.h"
#include "stdio.h"
#include "string.h"
#include "math.h"

#include "sd-task/sd-task.h"
#include "rec-task/rec-task.h"

/* ── Параметры генератора ────────────────────────────────────────────────── */

#define SAMPLE_RATE    44100u   /* Гц, частота дискретизации */
#define AMPLITUDE      28000    /* [-32768..32767], чуть ниже максимума */
#define B64_INPUT_BYTES 48      /* 48 байт → 64 base64 символа = одна строка */

/* ── WAV header (PCM, 16 бит, моно) ──────────────────────────────────────── */

static void write_u16le(FIL *f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v), (uint8_t)(v >> 8) };
    UINT bw;
    f_write(f, b, 2, &bw);
}

static void write_u32le(FIL *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v), (uint8_t)(v >> 8),
                     (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    UINT bw;
    f_write(f, b, 4, &bw);
}

static void wav_write_header(FIL *f, uint32_t num_samples)
{
    UINT bw;
    uint32_t data_size = num_samples * 2; /* 16-bit mono */
    uint32_t file_size = 36 + data_size;  /* total − 8 */

    f_write(f, "RIFF", 4, &bw);
    write_u32le(f, file_size);
    f_write(f, "WAVE", 4, &bw);

    f_write(f, "fmt ", 4, &bw);
    write_u32le(f, 16);              /* chunk size */
    write_u16le(f, 1);               /* PCM */
    write_u16le(f, 1);               /* mono */
    write_u32le(f, SAMPLE_RATE);
    write_u32le(f, SAMPLE_RATE * 2); /* byte rate */
    write_u16le(f, 2);               /* block align */
    write_u16le(f, 16);              /* bits per sample */

    f_write(f, "data", 4, &bw);
    write_u32le(f, data_size);
}

/* ── Нумерация файлов ─────────────────────────────────────────────────────── */

/* Сканирует корневой каталог, находит максимальный номер NNN в NNN.wav,
   записывает в out имя следующего файла (например "003.wav"). */
static void next_wav_name(FATFS *fs, char *out, size_t out_size)
{
    (void)fs;
    DIR dir;
    FILINFO fno;
    int max_num = 0;

    if (f_opendir(&dir, "/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            int num = 0;
            char ext[4];
            /* fname в SFN (8.3) выглядит как "001.WAV" */
            if (sscanf(fno.fname, "%d.%3s", &num, ext) == 2) {
                /* сравниваем без учёта регистра */
                if ((ext[0] == 'W' || ext[0] == 'w') &&
                    (ext[1] == 'A' || ext[1] == 'a') &&
                    (ext[2] == 'V' || ext[2] == 'v') &&
                    num > max_num) {
                    max_num = num;
                }
            }
        }
        f_closedir(&dir);
    }

    snprintf(out, out_size, "%03d.WAV", max_num + 1);
}

/* ── Base64 ──────────────────────────────────────────────────────────────── */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode_block(const uint8_t *in, int len, char out[4])
{
    out[0] = B64[in[0] >> 2];
    out[1] = B64[((in[0] & 0x03) << 4) | (len > 1 ? (in[1] >> 4) : 0)];
    out[2] = len > 1 ? B64[((in[1] & 0x0F) << 2) | (len > 2 ? (in[2] >> 6) : 0)] : '=';
    out[3] = len > 2 ? B64[in[2] & 0x3F] : '=';
}

/* ── API ──────────────────────────────────────────────────────────────────── */

void wav_task_init(void) { /* ничего не нужно — FatFS инициализируется через diskio */ }

/* wav_sine <freq_hz> <seconds>
   Генерирует синусоиду и записывает WAV-файл на SD карту. */
void wav_sine_callback(const char *args)
{
    if (rec_is_running()) { printf("busy: recording in progress\n"); return; }

    uint32_t freq = 440, seconds = 3;
    sscanf(args, "%lu %lu", &freq, &seconds);

    if (freq < 1 || freq > 20000) { printf("freq must be 1..20000 Hz\n"); return; }
    if (seconds < 1 || seconds > 300) { printf("seconds must be 1..300\n"); return; }

    sd_err_t serr = sd_task_ensure_init();
    if (serr != SD_OK) { printf("sd init error: %s\n", sd_err_str(serr)); return; }

    static FATFS fs;
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) { printf("mount error: %d\n", (int)fr); return; }

    char fname[16];
    next_wav_name(&fs, fname, sizeof(fname));

    FIL fil;
    fr = f_open(&fil, fname, FA_CREATE_NEW | FA_WRITE);
    if (fr != FR_OK) {
        printf("open '%s' error: %d\n", fname, (int)fr);
        f_mount(NULL, "", 0);
        return;
    }

    uint32_t num_samples = SAMPLE_RATE * seconds;
    wav_write_header(&fil, num_samples);

    /* Пишем синус блоками по 256 семплов (512 байт = один SD-блок) */
    static int16_t chunk[256];
    UINT bw;
    uint32_t written = 0;

    printf("Writing %s: %lu Hz, %lu sec, %lu samples...\n",
           fname, freq, seconds, num_samples);

    for (uint32_t i = 0; i < num_samples; i += 256) {
        uint32_t n = (i + 256 <= num_samples) ? 256 : (num_samples - i);
        for (uint32_t j = 0; j < n; j++) {
            float t = (float)(i + j) / (float)SAMPLE_RATE;
            chunk[j] = (int16_t)(AMPLITUDE * sinf(2.0f * 3.14159265f * (float)freq * t));
        }
        fr = f_write(&fil, chunk, n * 2, &bw);
        if (fr != FR_OK || bw != n * 2) {
            printf("write error at sample %lu\n", i);
            f_close(&fil);
            f_mount(NULL, "", 0);
            return;
        }
        written += n;

        /* Прогресс каждые ~10% */
        if (written % (num_samples / 10 + 1) < 256)
            printf("  %lu%%\n", written * 100 / num_samples);
    }

    f_close(&fil);
    f_mount(NULL, "", 0);

    printf("OK: %s (%lu bytes)\n", fname, 44 + num_samples * 2);
}

/* sd_ls — список WAV-файлов в корне */
void sd_ls_callback(const char *args)
{
    (void)args;
    if (rec_is_running()) { printf("busy: recording in progress\n"); return; }

    sd_err_t serr = sd_task_ensure_init();
    if (serr != SD_OK) { printf("sd init error: %s\n", sd_err_str(serr)); return; }

    static FATFS fs;
    FRESULT fr2 = f_mount(&fs, "", 1);
    if (fr2 != FR_OK) { printf("mount error: %d\n", (int)fr2); return; }

    DIR dir;
    FILINFO fno;
    int count = 0;

    if (f_opendir(&dir, "/") != FR_OK) {
        printf("opendir error\n");
        f_mount(NULL, "", 0);
        return;
    }

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        /* Показываем только WAV */
        char *dot = strrchr(fno.fname, '.');
        if (!dot) continue;
        if (strcmp(dot + 1, "WAV") != 0 && strcmp(dot + 1, "wav") != 0) continue;

        printf("%-12s %lu\n", fno.fname, (unsigned long)fno.fsize);
        count++;
    }
    f_closedir(&dir);
    f_mount(NULL, "", 0);

    printf("---\n%d file(s)\n", count);
}

/* sd_download <name.wav>
   Передаёт файл в base64 по serial. Python-скрипт декодирует.
   Протокол:
     DOWNLOAD:<name>:<size>\n
     <строки base64, 64 символа каждая>\n
     DONE\n */
void sd_download_callback(const char *args)
{
    if (rec_is_running()) { printf("busy: recording in progress\n"); return; }
    if (!args || args[0] == '\0') {
        printf("usage: sd_download <name.wav>\n");
        return;
    }

    /* Убираем пробелы в начале (протокол передаёт args без ведущего пробела) */
    while (*args == ' ') args++;

    sd_err_t serr = sd_task_ensure_init();
    if (serr != SD_OK) { printf("sd init error: %s\n", sd_err_str(serr)); return; }

    static FATFS fs;
    FRESULT fr3 = f_mount(&fs, "", 1);
    if (fr3 != FR_OK) { printf("mount error: %d\n", (int)fr3); return; }

    FIL fil;
    if (f_open(&fil, args, FA_READ) != FR_OK) {
        printf("file '%s' not found\n", args);
        f_mount(NULL, "", 0);
        return;
    }

    FSIZE_t fsize = f_size(&fil);
    printf("DOWNLOAD:%s:%lu\n", args, (unsigned long)fsize);

    static uint8_t rbuf[B64_INPUT_BYTES];
    UINT br;

    do {
        f_read(&fil, rbuf, B64_INPUT_BYTES, &br);
        if (br == 0) break;

        /* Кодируем br байт → максимум 64 символа base64 */
        char line[65];
        int p = 0;
        for (UINT i = 0; i < br; i += 3) {
            UINT n = (br - i >= 3) ? 3 : (br - i);
            char b4[4];
            b64_encode_block(rbuf + i, (int)n, b4);
            line[p++] = b4[0]; line[p++] = b4[1];
            line[p++] = b4[2]; line[p++] = b4[3];
        }
        line[p] = '\0';
        printf("%s\n", line);
    } while (br == B64_INPUT_BYTES);

    f_close(&fil);
    f_mount(NULL, "", 0);

    printf("DONE\n");
}
