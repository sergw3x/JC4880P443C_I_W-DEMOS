#ifndef QR_TEXT_COMMANDS_H
#define QR_TEXT_COMMANDS_H

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Структура для параметров QR кода с текстом
 */
typedef struct {
    char *qr_data;          // Данные для QR кода
    char *text;             // Текст для отображения (поддерживает {newline} для переносов строк)
    lv_color_t qr_color;    // Цвет QR кода
    lv_color_t bg_color;    // Цвет фона
    lv_color_t text_color;  // Цвет текста
    uint16_t font_size;     // Размер шрифта текста (10-32px, по умолчанию 18)
    lv_text_align_t text_align; // Выравнивание текста (по умолчанию LV_TEXT_ALIGN_CENTER)
} qr_text_params_t;

// Глобальные переменные для доступа из других модулей
extern lv_obj_t *qr_text_qrcode_obj;
extern lv_obj_t *qr_text_label_obj;

/**
 * @brief Выполняет команду отображения QR кода с текстом
 * 
 * @param json_string Полная JSON строка с командой
 * @return esp_err_t ESP_OK при успехе, соответствующий код ошибки при неудаче
 */
esp_err_t execute_qr_text_command(const char *json_string);

/**
 * @brief Создает QR код с текстом на одном экране
 * 
 * @param params Параметры для отображения
 * @return true при успехе, false при неудаче
 */
bool create_qr_with_text(const qr_text_params_t *params);

/**
 * @brief Асинхронно создает QR код с текстом
 * 
 * @param data Указатель на параметры qr_text_params_t
 */
void async_create_qr_with_text(void *data);

/**
 * @brief Парсит JSON и извлекает параметры для QR кода с текстом
 * 
 * @param json_string JSON строка
 * @param params Указатель на структуру для заполнения
 * @return true при успехе, false при неудаче
 */
bool parse_qr_text_json(const char *json_string, qr_text_params_t *params);

/**
 * @brief Освобождает память из структуры параметров
 * 
 * @param params Указатель на структуру параметров
 */
void free_qr_text_params(qr_text_params_t *params);

#endif // QR_TEXT_COMMANDS_H
