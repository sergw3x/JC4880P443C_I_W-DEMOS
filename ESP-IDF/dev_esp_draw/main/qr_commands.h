#ifndef QR_COMMANDS_H
#define QR_COMMANDS_H

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Выполняет QR команду
 * 
 * @param json_string Полная JSON строка с командой (для доступа к цветам)
 * @return esp_err_t ESP_OK при успехе, соответствующий код ошибки при неудаче
 */
esp_err_t execute_qr_command(const char *json_string);

/**
 * @brief Создает QR код с заданными параметрами
 * 
 * @param data Данные для QR кода
 * @param qr_color Цвет QR кода
 * @param bg_color Цвет фона экрана
 * @return true при успехе, false при неудаче
 */
bool create_qr_code(const char *data, lv_color_t qr_color, lv_color_t bg_color);

/**
 * @brief Асинхронно создает QR код (для обратной совместимости)
 * 
 * @param data Указатель на данные QR кода
 */
void async_draw_qr(void *data);

#endif // QR_COMMANDS_H