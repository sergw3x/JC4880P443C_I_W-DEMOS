#ifndef CLEAR_COMMANDS_H
#define CLEAR_COMMANDS_H

#include "esp_err.h"

/**
 * @brief Выполняет команду очистки экрана
 * 
 * @return esp_err_t ESP_OK при успехе, соответствующий код ошибки при неудаче
 */
esp_err_t execute_clear_command(void);

#endif // CLEAR_COMMANDS_H