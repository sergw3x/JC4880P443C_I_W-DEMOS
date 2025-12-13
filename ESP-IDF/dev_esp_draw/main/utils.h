#ifndef DEV_ESP_DRAW_UTILS_H
#define DEV_ESP_DRAW_UTILS_H

#include <stdint.h>
#include "lvgl.h"
#include "esp_log.h"

const lv_font_t *get_font_by_size(uint16_t font_size);

const lv_color_t parse_hex_color(const char *color_str);

const char *process_text_formatting_simple(const char *text);

#endif //DEV_ESP_DRAW_UTILS_H
