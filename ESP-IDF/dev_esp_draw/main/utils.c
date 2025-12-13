
#include "utils.h"

extern const lv_font_t font_roboto_24_cyr;
extern const lv_font_t font_roboto_28_cyr;
extern const lv_font_t font_roboto_32_cyr;
extern const lv_font_t font_roboto_36_cyr;
extern const lv_font_t font_roboto_40_cyr;
extern const lv_font_t font_roboto_44_cyr;
extern const lv_font_t font_roboto_48_cyr;
extern const lv_font_t font_roboto_52_cyr;
extern const lv_font_t font_roboto_56_cyr;
extern const lv_font_t font_roboto_60_cyr;
extern const lv_font_t font_roboto_64_cyr;
extern const lv_font_t font_roboto_68_cyr;

/**
 * @brief Выбирает шрифт LVGL по размеру
 */
const lv_font_t *get_font_by_size(uint16_t font_size) {
    switch (font_size) {
        case 24:
            return &font_roboto_24_cyr;
        case 28:
            return &font_roboto_28_cyr;
        case 32:
            return &font_roboto_32_cyr;
        case 36:
            return &font_roboto_36_cyr;
        case 40:
            return &font_roboto_40_cyr;
        case 44:
            return &font_roboto_44_cyr;
        case 48:
            return &font_roboto_48_cyr;
        case 52:
            return &font_roboto_52_cyr;
        case 56:
            return &font_roboto_56_cyr;
        case 60:
            return &font_roboto_60_cyr;
        case 64:
            return &font_roboto_64_cyr;
        case 68:
            return &font_roboto_68_cyr;
        default:
            if (font_size < 24) return &font_roboto_24_cyr;
            if (font_size > 68) return &font_roboto_68_cyr;
            if (font_size <= 32) return &font_roboto_32_cyr;
            if (font_size <= 40) return &font_roboto_40_cyr;
            if (font_size <= 48) return &font_roboto_48_cyr;
            if (font_size <= 56) return &font_roboto_56_cyr;
            if (font_size <= 64) return &font_roboto_64_cyr;
            return &font_roboto_44_cyr;
    }
}

// Функция для парсинга hex цвета в формате "#RRGGBB" или "RRGGBB"
const lv_color_t parse_hex_color(const char *color_str) {
    if (color_str == NULL || strlen(color_str) < 6) {
        ESP_LOGW("parse_hex_color", "Invalid color string: %s", color_str ? color_str : "NULL");
        return lv_color_black(); // Возвращаем черный цвет по умолчанию
    }

    // Убираем символ # если он есть
    const char *hex_start = color_str;
    if (color_str[0] == '#') {
        hex_start = color_str + 1;
    }

    // Проверяем длину
    if (strlen(hex_start) != 6) {
        ESP_LOGW("parse_hex_color", "Invalid hex color format: %s (expected RRGGBB)", color_str);
        return lv_color_black();
    }

    // Парсим RGB компоненты
    char r_str[3] = {hex_start[0], hex_start[1], '\0'};
    char g_str[3] = {hex_start[2], hex_start[3], '\0'};
    char b_str[3] = {hex_start[4], hex_start[5], '\0'};

    uint8_t
            r = (uint8_t)
    strtol(r_str, NULL, 16);
    uint8_t
            g = (uint8_t)
    strtol(g_str, NULL, 16);
    uint8_t
            b = (uint8_t)
    strtol(b_str, NULL, 16);

    ESP_LOGI("parse_hex_color", "Parsed color %s -> RGB(%d,%d,%d)", color_str, r, g, b);

    return lv_color_make(r, g, b);
}


/**
 * @brief Обрабатывает текст, заменяя маркеры {newline} на реальные переносы строк //todo: возможно можно убрать так как \n читается сейчас
 */
const char *process_text_formatting_simple(const char *text) {
    if (text == NULL) {
        return NULL;
    }

    // Подсчитываем количество маркеров {newline}
    size_t newline_count = 0;
    const char *pos = text;
    while ((pos = strstr(pos, "{newline}")) != NULL) {
        newline_count++;
        pos += 9; // Длина строки "{newline}"
    }

    // Если маркеров нет, возвращаем копию исходного текста
    if (newline_count == 0) {
        return strdup(text);
    }

    // Вычисляем новую длину строки
    size_t original_len = strlen(text);
    size_t new_len = original_len - (newline_count * 9) + newline_count; // Заменяем "{newline}" на "\n"

    // Выделяем память для новой строки
    char *processed_text = malloc(new_len + 1);
    if (processed_text == NULL) {
        ESP_LOGE("process_text_formatting_simple", "Failed to allocate memory for processed text");
        return NULL;
    }

    // Копируем и заменяем маркеры
    const char *src = text;
    char *dst = processed_text;

    while (*src != '\0') {
        if (strncmp(src, "{newline}", 9) == 0) {
            *dst++ = '\n';
            src += 9;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';

    ESP_LOGI("process_text_formatting_simple", "Processed text with %zu newlines", newline_count);

    return processed_text;
}