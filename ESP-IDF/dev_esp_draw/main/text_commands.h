#ifndef TEXT_COMMANDS_H
#define TEXT_COMMANDS_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Выполняет текстовую команду отображения
 * 
 * @param text_data Текст для отображения
 * @return esp_err_t ESP_OK при успехе, соответствующий код ошибки при неудаче
 */
esp_err_t execute_text_command(const char *text_data);

/**
 * @brief Отображает текст на экране через LVGL
 * 
 * @param text Текст для отображения
 */
void display_text_lvgl(const char *text);

/**
 * @brief Асинхронно отображает текст (для обратной совместимости)
 * 
 * @param data Указатель на данные текста
 */
void async_display_text(void *data);

/**
 * @brief Скрывает текстовый объект
 */
void lv_label_hide(void);

/**
 * @brief Показывает текстовый объект
 */
void lv_label_show(void);

#endif // TEXT_COMMANDS_H