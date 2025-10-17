import serial
import time

serial_port = '/dev/tty.usbmodem1101'
baud_rate = 9600
# DATA_TO_SEND = '{"type": "qr", "data": "https://moskvarium.ru"}\n'
# DATA_TO_SEND = '{"type": "qr", "data": "https://example.com"}\n'

data = [
    '{"type": "qr", "data": "https://example.com"}\n',
    '{"type": "qr", "data": "https://moskvarium.ru"}\n',
    '{"type": "qr", "data": "https://google.com"}\n',
    '{"type": "qr", "data": "https://yahoo.com"}\n',
]

try:
    # Инициализация с таймаутом в 1 секунду
    ser = serial.Serial(serial_port, baud_rate, timeout=1)
    print(f"Opened serial port {ser.name}")
    time.sleep(2)  # Пауза для сброса устройства
    for row in data:

        # Отправляем команду
        message = row.encode('utf-8')
        ser.write(message)
        print(f"Отправлены данные: {row.strip()}")

        # Читаем ответ от устройства
        response = ser.readline()
        if response:
            # Декодируем и выводим ответ
            print(f"Received: {response.decode('utf-8').strip()}")
        else:
            print("Received: No response within timeout")
        time.sleep(1)

    ser.close()

except serial.SerialException as e:
    print(f"Error: {e}")

finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print("Serial port closed.")
