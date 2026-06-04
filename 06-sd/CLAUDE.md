# 06-sd: SD-рекордер на Raspberry Pi Pico

## Назначение проекта

Прошивка для Raspberry Pi Pico, которая:
1. Работает с microSD-картой по SPI (низкоуровневый тест, HAL-драйвер)
2. Записывает WAV-файлы через FatFS (синус по команде, реальный сигнал с АЦП)
3. Отдаёт файлы на компьютер через USB serial (base64-протокол)

Разрабатывается в три этапа — каждый следующий надстраивается поверх предыдущего, не убирая API.

---

## Аппаратура

**Плата:** Raspberry Pi Pico (RP2040)

**SD-модуль:** MicroSD Card Adapter с AMS1117-3.3 на борту

**Распиновка SPI1:**

| SD Card | Pico GPIO | Функция  |
|---------|-----------|----------|
| CS      | GP13      | Chip Select |
| SCK     | GP10      | Clock    |
| MOSI    | GP11      | Data Out |
| MISO    | GP12      | Data In  |
| GND     | GND       |          |
| VCC     | VBUS (pin 40) | **5В, обязательно** |

**АЦП:**

| Источник сигнала | Pico GPIO    |
|-----------------|--------------|
| Аналог 0–3.3В   | GPIO26 (ADC0, pin 31) |
| GND             | GND          |

> ⚠️ Модуль питать только от VBUS (5В). AMS1117-3.3 имеет dropout ~1.1В:
> при 3.3В на входе карта получает 2.2В и не инициализируется.

---

## Структура файлов

```
06-sd/
  main.c              — точка входа, таблица API, main loop
  diskio.c            — мост FatFS ↔ sd-driver
  CMakeLists.txt      — сборка проекта
  memmap_rp2040.ld    — линкер-скрипт (BSS вынесен в отдельный регион)
  pico_sdk_import.cmake
  sd_tool.py          — Python CLI для взаимодействия с прошивкой
  PLAN.md             — архитектура и план по этапам
  LOG.md              — подробный лог разработки с решёнными багами
  SPECS.md            — постановка задачи и требования
  SD карты и микроконтроллерами.md — справочник по физике SD и SPI-протоколу

  stdio-task/         — неблокирующий ввод строк по USB CDC
  led-task/           — мигание встроенного светодиода (ON/OFF/BLINK)
  sd-task/            — обёртка команд над sd-driver
  wav-task/           — генерация WAV-синуса, sd_ls, sd_download
  rec-task/           — запись с АЦП (двойная буферизация + Core 1)

../libs/
  protocol/           — диспетчер текстовых команд по serial
  sd-driver/          — HAL-драйвер SD по SPI (init/read_block/write_block)
  fatfs/              — обёртка FatFS (ff.c из pico-sdk/lib/tinyusb/lib/fatfs)
```

---

## Архитектура слоёв

```
Пользователь (terminal / sd_tool.py)
          ↓
    protocol-task     ← диспетчер текстовых команд
          ↓
   sd-task / wav-task / rec-task   ← бизнес-логика этапов
          ↓
       FatFS (ff.c)   ← файловая система FAT32
          ↓
      diskio.c        ← мост FatFS → sd-driver
          ↓
     sd-driver        ← чистый SPI HAL (init, read_block, write_block)
          ↓
   hardware_spi (pico-sdk)
```

**Ядра RP2040:**
```
Core 0: stdio-task → protocol-task → led-task   ← командный цикл
Core 1: DMA → write chunks → f_close            ← запись АЦП (только во время rec_start)
```

---

## Полный API (текстовые команды)

### Служебные
```
version              — имя устройства и версия прошивки
help                 — список команд
off / on / blink     — управление встроенным LED
```

### Этап 1 — низкоуровневый тест SD (всегда доступны)
```
sd_info              — инициализация + вербозный вывод типа карты
sd_debug             — дамп сырых SPI-ответов: CMD0/8/58/ACMD41
sd_test [block]      — запись паттерна, чтение, сравнение (default блок=2048)
sd_write_block <n>   — записать тестовый паттерн в блок n
sd_read_block <n>    — прочитать блок n и вывести в hex
```

### Этап 2 — FAT32 + WAV-синус
```
wav_sine <freq_hz> <seconds>   — сгенерировать WAV с синусом (→ NNN.WAV)
sd_ls                          — список WAV-файлов с размерами, заканчивается "---"
sd_download <name.wav>         — передать файл по serial (base64)
```

### Этап 3 — запись с АЦП
```
rec_start [seconds]  — начать запись (0 или без арг = до rec_stop; max = seconds)
rec_stop             — остановить запись, дозаписать, исправить WAV-заголовок
```

> Команды `sd_ls` и `sd_download` во время записи возвращают `busy: recording in progress`.

---

## Python-скрипт sd_tool.py

Зависимость: `pip install pyserial`

```bash
python sd_tool.py ls                  # список файлов на карте
python sd_tool.py download 001.WAV    # скачать один файл
python sd_tool.py download-all        # скачать все WAV-файлы
python sd_tool.py wav-sine 440 3      # записать синус 440 Гц, 3 сек
python sd_tool.py record 5            # записать 5 сек с GPIO26, ждать, скачать
python sd_tool.py record-start        # начать запись без лимита
python sd_tool.py record-stop         # остановить и скачать файл
```

Файлы сохраняются в `downloads/<YYYY-MM-DD_HH-MM-SS>/` с оригинальными именами.

Порт обнаруживается автоматически по VID `0x2E8A` (Raspberry Pi). Явно: `--port COM5`.

**Протокол скачивания файла (base64):**
```
DOWNLOAD:001.WAV:264644\n
<строки base64 по 64 символа>\n
...
DONE\n
```

---

## Сборка

```bash
cd 06-sd
mkdir build && cd build
cmake ..
make -j4
```

Вывод: `06_sd.uf2` — прошить через UF2 bootloader (BOOTSEL + подключить USB).

Линкер-скрипт `memmap_rp2040.ld` выносит BSS в отдельный регион — нужно для двух буферов АЦП (128 KB).

**SDK:** `pico-sdk` должен быть доступен. Путь задаётся через `PICO_SDK_PATH` или `pico_sdk_import.cmake` (подхватывает из переменной окружения).

**FatFS:** берётся из `pico-sdk/lib/tinyusb/lib/fatfs/source/` — отдельно не нужен.

---

## Ключевые технические решения

### Питание модуля SD — обязательно 5В
AMS1117-3.3 на модуле требует ≥4.3В на входе для стабильного 3.3В на выходе.
При питании от 3.3В Pico на карту приходит ~2.2В → карта зависает на ACMD41.
Линии данных остаются на 3.3В — GPIO Pico в безопасности.

### Таймауты SPI
- **Ожидание токена данных (read, 0xFE):** должен быть в реальном времени (`delay_ms`), не в количестве байтов. При 10 МГц счётчик байтов даёт 1.6 мс вместо нужных 200 мс.
- **Ожидание готовности после записи:** минимум **10 000 мс**. Дешёвые карты (Smartbuy SDHC 32 GB) уходят на erase до ~3 сек при каждом новом NAND-блоке (~4 MB).

### Нумерация WAV-файлов
`next_wav_name()` сканирует каталог, находит максимальный N в `NNN.WAV`, возвращает N+1.
Общий пул для `wav_sine` и `rec_start` — `001.WAV`, `002.WAV` и т.д. без разделения типов.

### Двойная буферизация АЦП
Два буфера по 32 000 × uint16_t (64 KB каждый = 128 KB итого):
```
DMA → buf[A]                      ← первый чанк
DMA → buf[B]  ‖  SD ← buf[A]     ← параллельно
DMA → buf[A]  ‖  SD ← buf[B]
...
```
Условие надёжности: DMA-чанк = 4 сек > max SD write = ~3 сек.
Результат: запись без ограничений по времени, ~128 KB RAM.

### Core 1 и синхронизация
```c
static volatile bool rec_stop_flag;  /* Core 0 → Core 1 */
static volatile bool rec_running;    /* Core 1 → Core 0 */
```
`volatile` запрещает кеширование в регистрах. `bool` — атомарен на Cortex-M0+, мьютекс не нужен.
FatFS не потокобезопасна: пока `rec_running == true`, все SD-команды на Core 0 отклоняются.

### Исправление WAV-заголовка после записи
Заголовок пишется с нулевыми размерами. После `rec_stop` Core 1 исправляет через `f_lseek`:
- offset 4 → `RIFF size = 36 + data_size`
- offset 40 → `data size`

### Параметры АЦП
- Частота: 8 000 Гц (`adc_set_clkdiv(5999.0f)` → 48 МГц / 6000 = 8 кГц точно)
- Преобразование: `sample = (raw − 2048) << 4` (12-bit → int16_t с центровкой)
- DMA захватывает напрямую из `adc_hw->fifo` без участия CPU

---

## Известные проблемы и диагностика

| Симптом | Причина | Решение |
|---------|---------|---------|
| ACMD41 зависает навсегда | Бракованная карта или питание 3.3В | Попробовать другую карту; питать от 5В |
| `mount error: 1` (FR_DISK_ERR) | Таймаут read токена слишком короткий | Проверить `TIMEOUT_READ_MS` в sd-driver |
| `write error: timeout` | NAND erase на SD занял >500 мс | `TIMEOUT_WRITE_MS` должен быть ≥10000 |
| Данные 0xFF по MISO | MISO не подключён или перепутан с MOSI | Проверить распиновку |
| WAV не воспроизводится | Размеры в заголовке = 0 | `rec_stop` не вызван или f_lseek не сработал |

**Команда `sd_debug`** позволяет диагностировать SPI без осциллографа: ожидаемые ответы — `CMD41 no CMD55 = 0x05`, `ACMD41 = 0x01` (пока идёт init), `0x00` (готово).

---

## Версия прошивки

`v0.3.0-stage3` — все три этапа реализованы и проверены на железе.
