#!/usr/bin/env python3
"""sd_tool.py — управление SD-рекордером через USB serial.

Использование:
    python sd_tool.py ls                          # список файлов на карте
    python sd_tool.py download 001.WAV            # скачать один файл
    python sd_tool.py download-all                # скачать все WAV-файлы
    python sd_tool.py wav-sine 440 3              # записать синус 440 Гц, 3 сек
    python sd_tool.py record 5                    # записать 5 сек с GPIO26/ADC0
    python sd_tool.py record-start                # начать запись (без ограничения)
    python sd_tool.py record-stop                 # остановить запись и скачать файл

Файлы сохраняются в  downloads/<дата-время>/  с оригинальными именами.

Зависимости:  pip install pyserial
"""

import argparse
import base64
import datetime
import os
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Установите pyserial:  pip install pyserial")
    sys.exit(1)


# ── Обнаружение порта ──────────────────────────────────────────────────────

def find_pico_port():
    """Автоматически найти COM-порт Raspberry Pi Pico."""
    for p in serial.tools.list_ports.comports():
        desc = (p.description or "").lower()
        dev  = (p.device or "").lower()
        # VID Raspberry Pi = 0x2E8A
        if getattr(p, 'vid', None) == 0x2E8A:
            return p.device
        if "usbmodem" in dev or "ttyacm" in dev or "pico" in desc:
            return p.device
    return None


# ── Базовая работа с портом ────────────────────────────────────────────────

def read_until(ser, marker, timeout=15.0):
    """Читать строки пока не встретится строка, начинающаяся с marker."""
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        lines.append(line)
        if line.startswith(marker):
            break
    return lines


def send_command(ser, cmd):
    """Отправить команду (добавить \n) и дождаться эха от протокол-таска."""
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    ser.flush()
    # Протокол-таск всегда печатает "received command: '<name>' with args: '...'"
    # Ждём эту строку — она гарантирует, что команда принята.
    read_until(ser, "received command:", timeout=5.0)


# ── Команды ────────────────────────────────────────────────────────────────

def cmd_ls(ser):
    """Вывести список WAV-файлов."""
    send_command(ser, "sd_ls")
    lines = read_until(ser, "---", timeout=10.0)
    for line in lines:
        if not line.startswith("received"):
            print(line)


def cmd_download(ser, filename, out_dir):
    """Скачать один файл по base64-протоколу."""
    os.makedirs(out_dir, exist_ok=True)

    send_command(ser, f"sd_download {filename}")

    # Ждём заголовок DOWNLOAD:<name>:<size>
    header = None
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        if line.startswith("DOWNLOAD:"):
            parts = line.split(":")
            if len(parts) >= 3:
                name = parts[1]
                size = int(parts[2])
                header = (name, size)
            break
        if line.startswith("file '") or line.startswith("mount"):
            print(f"Ошибка: {line}")
            return

    if header is None:
        print("Нет ответа от прошивки (таймаут)")
        return

    name, size = header
    out_path = os.path.join(out_dir, name)

    print(f"Скачиваю {name} ({size} байт)...", end="", flush=True)

    data = bytearray()
    deadline = time.monotonic() + size / 800 + 30  # ~800 байт/сек + запас
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        if line == "DONE":
            break
        # Игнорируем служебные строки
        if line.startswith("received") or not line:
            continue
        try:
            data += base64.b64decode(line)
        except Exception:
            pass

    with open(out_path, "wb") as f:
        f.write(bytes(data[:size]))

    print(f" сохранено: {out_path}")


def cmd_download_all(ser, out_dir):
    """Скачать все WAV-файлы."""
    send_command(ser, "sd_ls")
    lines = read_until(ser, "---", timeout=10.0)

    files = []
    for line in lines:
        if not line or line.startswith("received") or line.startswith("---"):
            continue
        parts = line.split()
        if parts and (parts[0].upper().endswith(".WAV")):
            files.append(parts[0])

    if not files:
        print("Файлов не найдено")
        return

    print(f"Найдено {len(files)} файл(ов), сохраняю в {out_dir}/")
    for fname in files:
        cmd_download(ser, fname, out_dir)


def cmd_wav_sine(ser, freq, seconds):
    """Запустить генерацию синуса на прошивке."""
    print(f"Генерирую синус {freq} Гц, {seconds} сек...")
    send_command(ser, f"wav_sine {freq} {seconds}")
    lines = read_until(ser, "OK:", timeout=seconds + 30)
    for line in lines:
        if not line.startswith("received"):
            print(line)


def cmd_record(ser, seconds, out_dir):
    """Записать аудио с GPIO26/ADC0 и скачать результат."""
    print(f"Запись {seconds} сек с GPIO26/ADC0...")
    send_command(ser, f"rec_start {seconds}")

    # Ждём сначала "Capture done." (захват завершён), потом "OK: NNN.WAV"
    # Таймаут: seconds (захват) + 60 сек (запись на SD с возможным NAND-стиранием)
    # seconds захвата + запас на NAND-стирания (каждые ~4 MB = ~30 сек по 8 кГц)
    deadline = time.monotonic() + seconds + seconds * 0.5 + 120
    fname = None
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        if line and not line.startswith("received"):
            print(line)
        if line.startswith("OK:"):
            # Формат: "OK: 003.WAV (128044 bytes)"
            parts = line.split()
            if len(parts) >= 2:
                fname = parts[1]
            break

    if fname is None:
        print("Таймаут ожидания OK от прошивки")
        return

    print(f"Скачиваю {fname}...")
    cmd_download(ser, fname, out_dir)


# ── Точка входа ────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="sd_tool.py — управление 06-sd прошивкой")
    parser.add_argument("command",
        choices=["ls", "download", "download-all", "wav-sine",
                 "record", "record-start", "record-stop"],
        help="команда")
    parser.add_argument("args", nargs="*",
        help="аргументы команды: download <файл> | wav-sine <Гц> <сек>")
    parser.add_argument("--port", help="COM-порт (авто-обнаружение если не задан)")
    parser.add_argument("--baud", default=115200, type=int)
    opts = parser.parse_args()

    port = opts.port or find_pico_port()
    if not port:
        print("Pico не найден. Укажите порт через --port /dev/ttyACM0")
        sys.exit(1)

    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = os.path.join("downloads", ts)

    print(f"Порт: {port}")

    with serial.Serial(port, opts.baud, timeout=1) as ser:
        time.sleep(0.3)  # даём порту подняться
        ser.reset_input_buffer()

        if opts.command == "ls":
            cmd_ls(ser)

        elif opts.command == "download":
            if not opts.args:
                print("Укажите имя файла: sd_tool.py download 001.WAV")
                sys.exit(1)
            cmd_download(ser, opts.args[0], out_dir)

        elif opts.command == "download-all":
            cmd_download_all(ser, out_dir)

        elif opts.command == "wav-sine":
            if len(opts.args) < 2:
                print("Использование: sd_tool.py wav-sine <freq_hz> <seconds>")
                sys.exit(1)
            cmd_wav_sine(ser, int(opts.args[0]), int(opts.args[1]))

        elif opts.command == "record":
            if not opts.args:
                print("Использование: sd_tool.py record <seconds>")
                sys.exit(1)
            cmd_record(ser, int(opts.args[0]), out_dir)

        elif opts.command == "record-start":
            seconds = int(opts.args[0]) if opts.args else 0
            send_command(ser, f"rec_start {seconds}")
            # Ждём подтверждение запуска (не OK — он придёт позже при rec_stop)
            lines = read_until(ser, "Recording started", timeout=5.0)
            for line in lines:
                if not line.startswith("received"):
                    print(line)

        elif opts.command == "record-stop":
            send_command(ser, "rec_stop")
            print("Останавливаю запись...")
            # Ждём OK: и скачиваем файл
            deadline = time.monotonic() + 120  # до 2 мин на дозапись последнего чанка
            fname = None
            while time.monotonic() < deadline:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if line and not line.startswith("received"):
                    print(line)
                if line.startswith("OK:"):
                    parts = line.split()
                    if len(parts) >= 2:
                        fname = parts[1]
                    break
            if fname:
                print(f"Скачиваю {fname}...")
                cmd_download(ser, fname, out_dir)
            else:
                print("Файл не получен (таймаут)")


if __name__ == "__main__":
    main()
