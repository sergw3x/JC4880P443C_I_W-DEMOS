#ifndef LVGL_UTILS_H
#define LVGL_UTILS_H

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Парсит hex цвет в формате "#RRGGBB" или "RRGGBB"
 * 
 * @param color_str Строка с цветом
 * @return lv_color_t Структура цвета LVGL
 */
lv_color_t parse_hex_color(const char *color_str);

/**
 * @brief Проверяет валидность LVGL объекта
 * 
 * @param obj Объект для проверки
 * @return true если объект валиден, false в противном случае
 */
bool is_lvgl_object_valid(lv_obj_t *obj);

/**
 * @brief Очищает экран и устанавливает цвет фона
 * 
 * @param bg_color Цвет фона для установки
 */
void clear_screen_with_background(lv_color_t bg_color);

/**
 * @brief Инициализирует стили для label объектов
 */
void init_label_styles(void);

/**
 * @brief Получает текущий активный экран LVGL
 * 
 * @return lv_obj_t* Указатель на активный экран или NULL при ошибке
 */
lv_obj_t* get_current_screen(void);

#endif // LVGL_UTILS_H