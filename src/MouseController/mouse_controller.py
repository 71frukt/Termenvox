import time
import serial
import pyautogui
from serial.tools import list_ports

BAUD = 115200

def find_port(prefer_hint=("cp210", "ch340", "ch910", "usb", "uart", "esp")):
    """Пытается автоматически найти порт ESP32 по описанию."""
    ports = list(list_ports.comports())
    if not ports:
        return None

    # 1) сначала ищем похожий на ESP32
    for p in ports:
        desc = (p.description or "").lower()
        if any(h in desc for h in prefer_hint):
            return p.device

    # 2) если не нашли — берём первый попавшийся
    return ports[0].device


def clamp(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else hi if v > hi else v


def parse_goto(line: str):
    """
    Парсим строку формата: 'GOTO x y'
    Возвращает (x, y) как float или None
    """
    parts = line.strip().split()
    if len(parts) != 3 or parts[0] != "GOTO":
        return None

    try:
        x = float(parts[1])
        y = float(parts[2])
    except ValueError:
        return None

    return x, y


def main():
    port = find_port()
    if port is None:
        print("COM-порты не найдены. Подключи ESP32 и попробуй снова.")
        return

    print(f"Открываю порт: {port} @ {BAUD}")
    ser = serial.Serial(port, BAUD, timeout=1)

    screen_w, screen_h = pyautogui.size()

    # Настройки поведения
    SMOOTH_ALPHA = 0.25   # сглаживание на стороне ПК (0..1), больше = быстрее реагирует
    DURATION = 0          # 0 = мгновенно; можно 0.01..0.05 для "мягкого" догоняния

    # Сглаженное положение в пикселях (чтобы курсор не дрожал)
    cur_x, cur_y = pyautogui.position()

    print("Слушаю 'GOTO x y'. Ctrl+C чтобы выйти.")

    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode(errors="ignore").strip()
            if not line:
                continue

            data = parse_goto(line)
            if data is None:
                continue

            x_norm, y_norm = data

            x_norm = clamp(x_norm, 0.0, 1.0)
            y_norm = clamp(y_norm, 0.0, 1.0)


            SAFE_MARGIN = 5  # пикселей от края экрана

            target_x = int(x_norm * (screen_w - 1))
            target_y = int(y_norm * (screen_h - 1))

            # Ограничиваем, чтобы не попасть в угол
            target_x = max(SAFE_MARGIN, min(screen_w - SAFE_MARGIN, target_x))
            target_y = max(SAFE_MARGIN, min(screen_h - SAFE_MARGIN, target_y))

            cur_x = cur_x + (target_x - cur_x) * SMOOTH_ALPHA
            cur_y = cur_y + (target_y - cur_y) * SMOOTH_ALPHA

            pyautogui.moveTo(int(cur_x), int(cur_y), duration=DURATION)

    except KeyboardInterrupt:
        print("\nВыход.")
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
