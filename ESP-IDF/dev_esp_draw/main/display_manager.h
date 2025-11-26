#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_ops.h"

// Глобальные переменные для доступа из других модулей
extern lv_obj_t *label_obj;
#if LV_USE_QRCODE
    extern lv_obj_t *qrcode_obj;
#endif

/**
 * @brief Создает и настраивает label объект с заданным текстом
 * 
 * @param text Текст для отображения
 * @param bg_color Цвет фона экрана
 * @return true при успехе, false при неудаче
 */
bool create_label_with_text(const char *text, lv_color_t bg_color);

/**
 * @brief Управляет видимостью LVGL объекта
 * 
 * @param obj Объект LVGL
 * @param visible Видимость объекта (true = показать, false = скрыть)
 * @param obj_name Имя объекта для логирования
 */
void lv_obj_set_visibility(lv_obj_t *obj, bool visible, const char* obj_name);

/**
 * @brief Безопасно удаляет label объект
 */
void safe_label_delete(void);

/**
 * @brief Безопасно удаляет QR объект
 */
void safe_qrcode_delete(void);

/**
 * @brief Принудительно обновляет дисплей
 */
void refresh_display(void);

/**
 * @brief Получает глобальный мьютекс LVGL
 * 
 * @return SemaphoreHandle_t Указатель на мьютекс или NULL при ошибке
 */
SemaphoreHandle_t get_lvgl_mutex(void);

/**
 * @brief Получает глобальный объект дисплея LVGL
 * 
 * @return lv_disp_t* Указатель на объект дисплея или NULL при ошибке
 */
lv_disp_t* get_lvgl_display(void);

/**
 * @brief Получает handle LCD панели
 * 
 * @return esp_lcd_panel_handle_t Handle панели или NULL при ошибке
 */
esp_lcd_panel_handle_t get_display_panel(void);

/**
 * @brief Инициализирует стили для label объектов
 */
void init_label_styles(void);

/**
 * @brief Устанавливает глобальный объект дисплея LVGL
 * 
 * @param disp Указатель на объект дисплея
 */
void set_lvgl_display(lv_disp_t *disp);

/**
 * @brief Устанавливает глобальный мьютекс LVGL
 * 
 * @param mutex Указатель на мьютекс
 */
void set_lvgl_mutex(SemaphoreHandle_t mutex);

#endif // DISPLAY_MANAGER_H