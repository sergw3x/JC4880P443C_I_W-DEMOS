#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

/**
 * @brief Инициализирует LCD дисплей
 */
void init_lcd(void);

/**
 * @brief Инициализирует подсветку дисплея
 */
void init_backlight(void);

/**
 * @brief Инициализирует всю аппаратную часть
 * 
 * @return esp_err_t ESP_OK при успехе, соответствующий код ошибки при неудаче
 */
esp_err_t init_hardware(void);

/**
 * @brief Получает handle LCD панели
 * 
 * @return esp_lcd_panel_handle_t Handle панели или NULL при ошибке
 */
esp_lcd_panel_handle_t get_display_panel(void);

#endif // HARDWARE_MANAGER_H