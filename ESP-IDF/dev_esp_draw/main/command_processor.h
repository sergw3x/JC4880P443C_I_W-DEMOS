#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Обрабатывает входящие JSON команды и маршрутизирует их к соответствующим модулям
 * 
 * @param data JSON строка с командой
 * @return esp_err_t ESP_OK при успехе, соответствующий код ошибки при неудаче
 */
esp_err_t process_command(const char *data);

/**
 * @brief Валидирует JSON структуру команды
 * 
 * @param json_string JSON строка для валидации
 * @return true если структура корректна, false в противном случае
 */
bool validate_command_json(const char *json_string);

/**
 * @brief Извлекает тип команды из JSON
 * 
 * @param json_string JSON строка
 * @return указатель на строку с типом команды или NULL при ошибке
 */
const char* extract_command_type(const char *json_string);

/**
 * @brief Извлекает данные команды из JSON
 * 
 * @param json_string JSON строка  
 * @return указатель на строку с данными или NULL при ошибке
 */
const char* extract_command_data(const char *json_string);

/**
 * @brief Проверяет максимальную длину данных команды
 * 
 * @param data данные для проверки
 * @return true если длина допустима, false в противном случае
 */
bool validate_command_data_length(const char *data);

#endif // COMMAND_PROCESSOR_H