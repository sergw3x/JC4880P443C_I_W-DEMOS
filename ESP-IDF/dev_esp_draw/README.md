ESP32 QR Code Display
================================================

## Описание проекта

ESP32 приложение для отображения QR-кодов и текста на LCD дисплее через последовательный порт.
На входе ожидается JSON строка. 
**!Важно!** Ожидается, что в конце строки будет **\n** (_символ переноса_) и тогда начинается обработка json. Следовательно, нужно передавать json в одну строку 😉

## Features

* LVGL Demo v8
* `MIPI_DSI` interface
* ESP32-P4
* OV02C10 - color CMOS 2 megapixel image sensor
* 4.3 inch ESP32P4 module JC4880P443C_I_W/Y
* 4.3-inch color screen, support 24 BIT RGB 16.7M color display, display
  rich colors
* IPS 480x800 resolution
* Driver chip ST7701S
* The sample program has been programmed in the factory and can be plugged in
* With TF card slot for easy expansion storage
* Provide arduino library functions and sample programs to facilitate
  rapid secondary development
* Support one-click download program
* Lithium battery interface circuit
* Military-grade process standards, long-term stable work

```bash
idf.py -p PORT flash monitor
```

To exit the serial monitor, type ``Ctrl-]``.

## Поддерживаемые режимы

* type: **qr**
* type: **text**
* type: **qr_text**
* type: **clear**

## ✅ type: qr

### 1. QR-код

```json
{
  "type": "qr",
  "data": "https://example.com"
}
```

```json
{
  "type": "qr",
  "data": "WIFI:T:WPA;S:MyNetwork;P:password123;;",
  "qr_color": "#0066CC",
  "bg_color": "#F0F8FF"
}
```

- Отображает QR-код с красными пикселями на зеленом фоне экрана
- `qr_color` - цвет пикселей QR кода
- `bg_color` - цвет фона всего экрана (не только QR области)
- Поддерживает hex цвета в формате #RRGGBB или RRGGBB
- Фон экрана остается белым
- Только цвет фона экрана (желтый)
- QR код остается черным

## ✅ type: text

```json
{
  "type": "text",
  "data": "Hello World"
}
```

Простой текст на кириллице

```json
{
  "type": "text",
  "data": "Привет, мир! ESP32 с кириллицей!"
}
```

- Отображает текст в центре экрана

## Шрифты

для генерации внутри скрипта ``generate_lvgl_font.sh`` используется [lv_font_conv](https://github.com/lvgl/lv_font_conv)
— сгенерирует шрифты от 12 до 40 с шагом 4.

### Генерация набора шрифтов

```bash
./generate_lvgl_font.sh --start 20 --end 32 --step 4
```

### Генерация конкретного щрифта

```bash
lv_font_conv --font fonts/Roboto/static/Roboto-Regular.ttf --size 24 --bpp 4 --format lvgl --range 0x20-0x7E,0xA0,0x0400-0x04FF --output main/fonts/font_roboto_24_cyr.c
```

## ✅ type: qr_text

```json
{
  "type": "qr_text",
  "data": "https://company.com",
  "text": "Welcome, Привет{newline}Please, visit our 1-й сайт $1234567890",
  "qr_color": "#00FF99",
  "font_size": 40,
  "text_align": "left",
  "bg_color"  :"#000000",
  "text_color":"#CC9933"
}
```

- {newline} - перенос строки
- "font_size" : 48 ``размеры 24 - 68 с шагом 4px``
- "text_align": "left"  ``выравнивание текста``
- "text_color":"#0000FF" ``цвет текста``
- "qr_color"  :"#000000" ``цвет qr``
- "bg_color"  :"#FFFFFF" ``цвет фона``

## ✅ type: clear

```json
{
  "type": "clear",
  "data": ""
}
```