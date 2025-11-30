# Решение проблем с длинными QR-командами

## Проблема
Пользователь отправлял команду:
```json
{"type":"qr","data":"Расширенный QR-код с настройками","qr_color":"#FF4500","bg_color":"#2F4F4F","qr_bg_color":"#FFFFFF","border_width":8,"border_color":"#FFD700","top_text":"Верхняя надпись","bottom_text":"Нижняя надпись","text_color":"#000080","font_size":18}
```

Но получал ошибки:
```
W (86169) esp_draw_bit: USB buffer overflow detected (max 256 bytes), resetting
I (87139) esp_draw_bit: Received: яя надпись","text_color":"#000080","font_size":18}
E (87139) esp_draw_bit: failed to parse string as JSON
```

## Причины проблемы

### 1. **Недостаточный размер USB буфера**
- Исходный лимит: 256 байт
- Размер команды: ~280+ байт  
- Результат: переполнение и обрезка команды

### 2. **Несовместимость формата**
- Отправлено: `"type":"qr"`
- Ожидается: `"command": "qr"`

### 3. **Несуществующие параметры**
- `top_text`, `bottom_text` (должны быть `title`, `subtitle`)
- `qr_bg_color`, `border_width`, `font_size` (не реализованы)

## Решения

### ✅ **1. Увеличен размер USB буфера**
```c
// Было:
#define USB_RX_BUFFER_SIZE 256

// Стало:  
#define USB_RX_BUFFER_SIZE 2048
```
**Результат**: Команды до 2KB теперь принимаются без проблем

### ✅ **2. Добавлено улучшенное логирование**
```c
ESP_LOGI(TAG, "USB command received: %d bytes", index);
if (index > 100) {
    ESP_LOGI(TAG, "Command preview: %.100s...", buffer);
} else {
    ESP_LOGI(TAG, "Received: %s", buffer);
}
```

## Работающие команды

### ✅ **Базовый QR-код:**
```json
{"command": "qr", "data": "https://example.com"\}
```

### ✅ **QR-код с текстовыми подписями:**
```json
{"command": "qr", "data": "Тест QR-кода", "title": "Мой заголовок", "subtitle": "Мой подзаголовок"}
```

### ✅ **QR-код с цветами:**
```json
{"command": "qr", "data": "Тест QR-кода", "title": "Заголовок", "subtitle": "Подзаголовок", "qr_color": "#FF4500", "bg_color": "#2F4F4F"}
```

### ❌ **Не поддерживается (для примера):**
```json
{"type": "qr", ...}           // Неправильный ключ команды  
"top_text"/"bottom_text"      // Нужно использовать "title"/"subtitle"
"qr_bg_color", "font_size"    // Не реализовано
```

## Статус реализации

- [x] Увеличение USB буфера до 2048 байт
- [x] Улучшенное логирование команд  
- [ ] Добавление таймаута для неполных команд
- [ ] Поддержка PSRAM для динамических буферов  
- [ ] Обратная совместимость с форматом "type":"qr"
