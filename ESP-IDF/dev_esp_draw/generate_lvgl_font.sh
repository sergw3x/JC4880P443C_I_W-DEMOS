#!/bin/bash

# Настройки по умолчанию
INPUT_FONT="fonts/Roboto-Regular.ttf"
OUTPUT_DIR="main/fonts"
FONT_NAME="roboto"
RANGE="0x20-0x7E,0xA0,0x0400-0x04FF"
BPP=4

# Параметры командной строки
START_SIZE=12
END_SIZE=40
STEP=4

# Обработка аргументов
while [[ $# -gt 0 ]]; do
  case $1 in
    -s|--start) START_SIZE="$2"; shift 2 ;;
    -e|--end)   END_SIZE="$2"; shift 2 ;;
    -t|--step)  STEP="$2"; shift 2 ;;
    -f|--font)  INPUT_FONT="$2"; shift 2 ;;
    -o|--out)   OUTPUT_DIR="$2"; shift 2 ;;
    -n|--name)  FONT_NAME="$2"; shift 2 ;;
    -h|--help)
      echo "Использование: $0 [ОПЦИИ]"
      echo "Опции:"
      echo "  -s, --start SIZE    Начальный размер шрифта (по умолчанию: 12)"
      echo "  -e, --end SIZE      Конечный размер шрифта (по умолчанию: 40)"
      echo "  -t, --step STEP     Шаг между размерами (по умолчанию: 4)"
      echo "  -f, --font PATH     Путь к TTF-файлу (по умолчанию: fonts/Roboto/static/Roboto-Regular.ttf)"
      echo "  -o, --out DIR       Папка вывода (по умолчанию: main/fonts)"
      echo "  -n, --name NAME     Базовое имя шрифта (по умолчанию: roboto)"
      exit 0
      ;;
    *)
      echo "Неизвестный аргумент: $1"
      exit 1
      ;;
  esac
done

# Создаём выходную директорию, если её нет
mkdir -p "$OUTPUT_DIR"

# Проверяем наличие lv_font_conv
if ! command -v lv_font_conv &> /dev/null; then
  echo "Ошибка: lv_font_conv не найден. Установите его через:"
  echo "  npm install -g lv_font_conv"
  exit 1
fi

# Проверяем существование TTF-файла
if [[ ! -f "$INPUT_FONT" ]]; then
  echo "Ошибка: TTF-файл не найден: $INPUT_FONT"
  exit 1
fi

# Генерация шрифтов
for (( size=START_SIZE; size<=END_SIZE; size+=STEP )); do
  output_file="$OUTPUT_DIR/font_${FONT_NAME}_${size}_cyr.c"
  echo "Генерация шрифта размером $size → $output_file"
  lv_font_conv \
    --font "$INPUT_FONT" \
    --size "$size" \
    --bpp "$BPP" \
    --range "$RANGE" \
    --format lvgl \
    --output "$output_file"
  if [[ $? -ne 0 ]]; then
    echo "Ошибка при генерации шрифта размером $size"
    exit 1
  fi
done

echo "✅ Все шрифты успешно сгенерированы в $OUTPUT_DIR"