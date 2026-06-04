#pragma once

void wav_task_init(void);

void wav_sine_callback(const char *args);   /* wav_sine <freq_hz> <seconds> */
void sd_ls_callback(const char *args);      /* список WAV-файлов на карте  */
void sd_download_callback(const char *args);/* sd_download <name.wav>       */
