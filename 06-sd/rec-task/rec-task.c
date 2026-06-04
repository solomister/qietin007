#include "rec-task.h"

#include "ff.h"
#include "stdio.h"
#include "string.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "pico/multicore.h"

#include "sd-task/sd-task.h"

/* ── Параметры ───────────────────────────────────────────────────────────────
   Двойная буферизация на двух ядрах:
     Core 0 — командный цикл (принимает rec_stop в любой момент)
     Core 1 — запись: DMA → buf[cur], SD ← buf[prev], проверяет rec_stop_flag

   Условие надёжности: DMA-время одного чанка > время записи на SD.
     Чанк 32 000 семплов при 8 кГц = 4 сек. Запись 64 KB на SD: max ~3 сек.
     Запас 1 сек → семплы не теряются даже при NAND-стирании. */

#define REC_SAMPLE_RATE    8000u
#define REC_MAX_SECONDS   3600u   /* 1 час — лимит команды, не памяти */
#define REC_CHUNK_SAMPLES 32000u  /* 4 сек на чанк */

#define REC_ADC_CHANNEL   0
#define REC_ADC_GPIO      26
#define REC_ADC_CLKDIV    (48000000.0f / REC_SAMPLE_RATE - 1.0f)

/* ── Двойной буфер ───────────────────────────────────────────────────────── */

static uint16_t adc_buf[2][REC_CHUNK_SAMPLES]; /* 128 KB в BSS */

/* ── Разделяемое состояние Core 0 ↔ Core 1 ──────────────────────────────── */

static volatile bool rec_stop_flag;   /* Core 0 выставляет, Core 1 читает */
static volatile bool rec_running;     /* Core 1 выставляет/сбрасывает      */

static struct {
    uint32_t max_samples; /* 0 = пока не придёт rec_stop */
} rec_params;

bool rec_is_running(void) { return rec_running; }

/* ── WAV-хелперы ─────────────────────────────────────────────────────────── */

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

static void wav_write_header(FIL *f, uint32_t sample_rate)
{
    UINT bw;
    /* Размеры — заглушки 0; исправим через f_lseek после записи. */
    f_write(f, "RIFF", 4, &bw);
    write_u32le(f, 0);               /* ← исправим: file_size */
    f_write(f, "WAVE", 4, &bw);

    f_write(f, "fmt ", 4, &bw);
    write_u32le(f, 16);
    write_u16le(f, 1);               /* PCM */
    write_u16le(f, 1);               /* моно */
    write_u32le(f, sample_rate);
    write_u32le(f, sample_rate * 2);
    write_u16le(f, 2);
    write_u16le(f, 16);

    f_write(f, "data", 4, &bw);
    write_u32le(f, 0);               /* ← исправим: data_size */
}

static void wav_fix_sizes(FIL *f, uint32_t num_samples)
{
    uint32_t data_size = num_samples * 2;
    uint32_t riff_size = 36 + data_size;

    f_lseek(f, 4);
    write_u32le(f, riff_size);

    f_lseek(f, 40);
    write_u32le(f, data_size);
}

static void next_wav_name(char *out, size_t out_size)
{
    DIR dir;
    FILINFO fno;
    int max_num = 0;

    if (f_opendir(&dir, "/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            int num = 0;
            char ext[4];
            if (sscanf(fno.fname, "%d.%3s", &num, ext) == 2) {
                if ((ext[0] == 'W' || ext[0] == 'w') &&
                    (ext[1] == 'A' || ext[1] == 'a') &&
                    (ext[2] == 'V' || ext[2] == 'v') &&
                    num > max_num)
                    max_num = num;
            }
        }
        f_closedir(&dir);
    }
    snprintf(out, out_size, "%03d.WAV", max_num + 1);
}

static FRESULT write_chunk(FIL *fil, const uint16_t *buf, uint32_t n)
{
    static int16_t chunk[256];
    UINT bw;

    for (uint32_t i = 0; i < n; i += 256) {
        uint32_t blk = (i + 256 <= n) ? 256 : (n - i);
        for (uint32_t j = 0; j < blk; j++)
            chunk[j] = (int16_t)(((int32_t)buf[i + j] - 2048) << 4);
        FRESULT fr = f_write(fil, chunk, blk * 2, &bw);
        if (fr != FR_OK || bw != blk * 2)
            return FR_DISK_ERR;
    }
    return FR_OK;
}

/* ── Core 1: тело записи ─────────────────────────────────────────────────── */

static void core1_recording(void)
{
    /* Монтируем FS и открываем файл. */
    sd_err_t serr = sd_task_ensure_init();
    if (serr != SD_OK) {
        printf("rec: sd init error: %s\n", sd_err_str(serr));
        rec_running = false;
        return;
    }

    static FATFS fs;
    if (f_mount(&fs, "", 1) != FR_OK) {
        printf("rec: mount error\n");
        rec_running = false;
        return;
    }

    char fname[16];
    next_wav_name(fname, sizeof(fname));

    FIL fil;
    if (f_open(&fil, fname, FA_CREATE_NEW | FA_WRITE) != FR_OK) {
        printf("rec: open error\n");
        f_mount(NULL, "", 0);
        rec_running = false;
        return;
    }

    wav_write_header(&fil, REC_SAMPLE_RATE);
    printf("Recording %s (8 kHz mono, rec_stop to finish)...\n", fname);

    /* Настройка АЦП + DMA. */
    adc_init();
    adc_gpio_init(REC_ADC_GPIO);
    adc_select_input(REC_ADC_CHANNEL);
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(REC_ADC_CLKDIV);

    int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    uint32_t total_samples = 0;
    uint32_t remaining = rec_params.max_samples; /* 0 = бесконечно */
    bool ok = true;
    int cur = 0;

    /* Первый чанк: просто захватываем, нечего ещё писать. */
    uint32_t cur_n = REC_CHUNK_SAMPLES;
    if (remaining > 0 && cur_n > remaining)
        cur_n = remaining;
    if (remaining > 0)
        remaining -= cur_n;

    dma_channel_configure(dma_chan, &cfg,
        adc_buf[cur], &adc_hw->fifo, cur_n, true);
    adc_run(true);
    dma_channel_wait_for_finish_blocking(dma_chan);

    /* Основной цикл — продолжаем пока не придёт rec_stop или не кончится время. */
    while (!rec_stop_flag && (remaining > 0 || rec_params.max_samples == 0)) {
        int next = 1 - cur;
        uint32_t next_n = REC_CHUNK_SAMPLES;
        if (remaining > 0 && next_n > remaining)
            next_n = remaining;
        if (remaining > 0)
            remaining -= next_n;

        /* Немедленно запускаем DMA на следующий буфер. */
        dma_channel_configure(dma_chan, &cfg,
            adc_buf[next], &adc_hw->fifo, next_n, true);

        /* Пишем текущий чанк, пока DMA заполняет следующий. */
        if (write_chunk(&fil, adc_buf[cur], cur_n) != FR_OK) {
            printf("rec: write error at %lu samples\n", total_samples);
            ok = false;
            break;
        }
        total_samples += cur_n;

        dma_channel_wait_for_finish_blocking(dma_chan);
        cur = next;
        cur_n = next_n;
    }

    adc_run(false);
    adc_fifo_drain();
    dma_channel_unclaim(dma_chan);

    /* Пишем последний чанк (частично заполненный при rec_stop). */
    if (ok) {
        /* При rec_stop DMA уже завершён (wait выше), но cur_n может быть
           меньше REC_CHUNK_SAMPLES — берём только реально захваченные семплы.
           Если остановлены флагом, DMA успел заполнить cur_n семплов полностью
           (мы ждали dma_wait). Пишем как есть. */
        if (write_chunk(&fil, adc_buf[cur], cur_n) != FR_OK) {
            printf("rec: write error at %lu samples\n", total_samples);
            ok = false;
        } else {
            total_samples += cur_n;
        }
    }

    /* Исправляем заголовок WAV с реальным числом семплов. */
    if (ok) {
        wav_fix_sizes(&fil, total_samples);
    }

    f_close(&fil);
    f_mount(NULL, "", 0);

    if (ok)
        printf("OK: %s (%lu bytes, %.1f sec)\n",
               fname,
               44 + total_samples * 2,
               (float)total_samples / REC_SAMPLE_RATE);

    rec_running = false;
}

/* ── API ─────────────────────────────────────────────────────────────────── */

void rec_task_init(void) {}

void rec_start_callback(const char *args)
{
    if (rec_running) {
        printf("already recording — send rec_stop first\n");
        return;
    }

    uint32_t seconds = 0;
    sscanf(args, "%lu", &seconds);

    if (seconds > REC_MAX_SECONDS) {
        printf("seconds must be 0..%u (0 = until rec_stop)\n", REC_MAX_SECONDS);
        return;
    }

    rec_params.max_samples = seconds * REC_SAMPLE_RATE;
    rec_stop_flag = false;
    rec_running = true;

    multicore_reset_core1();
    multicore_launch_core1(core1_recording);

    if (seconds > 0)
        printf("Recording started (max %lu sec). Send rec_stop to finish early.\n", seconds);
    else
        printf("Recording started (unlimited). Send rec_stop to finish.\n");
}

void rec_stop_callback(const char *args)
{
    (void)args;
    if (!rec_running) {
        printf("not recording\n");
        return;
    }
    rec_stop_flag = true;
    printf("Stopping... waiting for last chunk to save.\n");
}
