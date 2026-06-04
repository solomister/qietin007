# Plan: SD-карта + WAV-рекордер на Raspberry Pi Pico

## Этап 1 — Низкоуровневый тест SD-карты

Цель: убедиться, что физическое соединение и SPI-протокол работают. Прошивка должна уметь:
- инициализировать карту по SPI
- записать паттерн в один или несколько блоков
- прочитать их обратно и сравнить с эталоном
- вывести результат в терминал

Структура проекта:
```
06-sd/
  main.c                   — API-команды + инициализация
  CMakeLists.txt
  memmap_rp2040.ld
  pico_sdk_import.cmake
  stdio-task/              — стандартный ввод из USB CDC
  led-task/                — мигание светодиода
  sd-task/                 — команды SD-карты (обёртка над драйвером)

libs/
  sd-driver/               — чистый HAL-драйвер SD по SPI
    include/sd-driver.h
    src/sd-driver.c
    CMakeLists.txt
```

Распиновка (SPI1, Pico):
```
SD Card  │  Pico GPIO
─────────┼──────────
CS       │  GP13
SCK      │  GP10
MOSI     │  GP11
MISO     │  GP12
GND      │  GND
VCC      │  VBUS (pin 40, 5В)  ⚠️ ТОЛЬКО 5В — см. ниже
```

> ⚠️ **ВНИМАНИЕ: питать модуль только от VBUS (5В), не от 3V3!**
> На модуле стоит стабилизатор AMS1117-3.3. При питании от 3.3В Pico на карту приходит
> только 2.2В (dropout ~1.1В) — карта не инициализируется. При 5В на выходе AMS1117
> ровно 3.3В. Линии данных при этом остаются на 3.3В, GPIO Pico в безопасности.

API-команды этапа 1:
```
sd_info                     — инициализация + вывод типа карты
sd_test [block]             — запись паттерна, чтение, сравнение (по умолчанию блок 2048)
sd_write_block <block>      — записать тестовый паттерн в конкретный блок
sd_read_block <block>       — прочитать и вывести блок в hex
```

Тест-паттерн: 512 байт, где `buf[i] = i & 0xFF`, первые 4 байта — номер блока.

## Этап 2 — FAT32 + запись WAV-файлов с синусом ✅

Структура новых компонентов:
```
06-sd/
  diskio.c                 — мост FatFS ↔ sd_driver (FatFS disk I/O interface)
  wav-task/                — генерация WAV + файловые команды
  sd_tool.py               — Python CLI (ls / download / download-all / wav-sine)

libs/
  fatfs/                   — обёртка FatFS из pico-sdk/lib/tinyusb/lib/fatfs
    CMakeLists.txt
    ffconf.h               (используется bundled из pico-sdk — не переопределяет)
```

API-команды этапа 2 (добавляются, команды этапа 1 остаются):
```
wav_sine <freq_hz> <seconds>    — сгенерировать WAV с синусом (001.WAV, 002.WAV...)
sd_ls                           — список файлов: имя + размер, потом "---"
sd_download <name.wav>          — скачать файл base64 по serial
```

Протокол скачивания:
```
DOWNLOAD:<name>:<size>\n
<строки base64 по 64 символа>\n
DONE\n
```

Python-скрипт:
```
python sd_tool.py ls
python sd_tool.py wav-sine 440 3
python sd_tool.py download 001.WAV
python sd_tool.py download-all
```

## Этап 3 — Запись с АЦП ✅

Подключение:
```
GPIO26 (ADC0, пин 31)  ← аналоговый сигнал 0–3.3В
GND                    ← общий
```
> Для переменного сигнала (генератор, микрофон) — сдвиг постоянного тока к ~1.65В
> через резисторный делитель или ёмкостная развязка + подтяжка к середине.

Структура новых компонентов:
```
06-sd/
  rec-task/
    rec-task.h          — интерфейс: rec_start, rec_stop, rec_is_running()
    rec-task.c          — Core 1: DMA + запись WAV, volatile-флаги синхронизации
```

API-команды этапа 3:
```
rec_start [seconds]     — начать запись (0 или без арг. = до rec_stop, иначе макс. N сек)
rec_stop                — остановить запись, сохранить WAV с корректным заголовком
```

Архитектура ядер:
```
Core 0: stdio → protocol_task → led_task      ← как всегда
Core 1: ADC DMA → запись чанков → f_close    ← запускается командой rec_start
```

Синхронизация:
```c
volatile bool rec_stop_flag;   /* Core 0 → Core 1 */
volatile bool rec_running;     /* Core 1 → Core 0 */
```

Двойная буферизация (два буфера по 32 000 uint16_t = 128 KB):
```
DMA → buf[0]                    (ждём первый чанк)
DMA → buf[1]  ‖  SD ← buf[0]   (параллельно)
DMA → buf[0]  ‖  SD ← buf[1]
...
```
Условие надёжности: DMA 4 сек > запись на SD 3 сек (max NAND-стирание).

WAV-заголовок с `f_lseek`: размеры записываются как 0 в начале, исправляются
после остановки. Файл корректен при любой длительности.

Python-скрипт:
```
python sd_tool.py record 5        # фиксированная длительность, блокирующий
python sd_tool.py record-start    # старт без лимита
python sd_tool.py record-stop     # стоп + скачать файл
```
