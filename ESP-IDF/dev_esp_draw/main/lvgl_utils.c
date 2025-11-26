#include "lvgl_utils.h"
#include "esp_log.h"

static const char *TAG = "lvgl_utils";

// Глобальные переменные LVGL (для совместимости)
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

lv_color_t parse_hex_color(const char *color_str) {
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
    
    uint8_t r = (uint8_t)strtol(r_str, NULL, 16);
    uint8_t g = (uint8_t)strtol(g_str, NULL, 16);
    uint8_t b = (uint8_t)strtol(b_str, NULL, 16);
    
    ESP_LOGI("parse_hex_color", "Parsed color %s -> RGB(%d,%d,%d)", color_str, r, g, b);
    
    return lv_color_make(r, g, b);
}

bool is_lvgl_object_valid(lv_obj_t *obj) {
    if (obj == NULL) {
        return false;
    }
    
    // Проверяем валидность объекта через LVGL API
    return lv_obj_is_valid(obj);
}

void clear_screen_with_background(lv_color_t bg_color) {
    lv_obj_t *current_screen = get_current_screen();
    if (current_screen != NULL) {
        lv_obj_clean(current_screen);
        lv_obj_set_style_bg_color(current_screen, bg_color, 0);
    }
}



lv_obj_t* get_current_screen(void) {
    return lv_scr_act();
}

// Функции для управления буферами (для совместимости с main.c)
void set_lvgl_buffers(lv_color_t *buffer1, lv_color_t *buffer2) {
    buf1 = buffer1;
    buf2 = buffer2;
}

lv_color_t* get_lvgl_buffer1(void) {
    return buf1;
}

lv_color_t* get_lvgl_buffer2(void) {
    return buf2;
}