#include "command_processor.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

// Включаем модули команд
#include "text_commands.h"
#include "qr_commands.h" 
#include "clear_commands.h"
#include "qr_text_commands.h"

static const char *TAG = "command_processor";

// Максимальная длина данных команды
#define COMMAND_DATA_MAX_LENGTH 1024

esp_err_t process_command(const char *data) {
    ESP_LOGI(TAG, "Received command: %s", data);

    // Проверка входных параметров
    if (data == NULL || strlen(data) > COMMAND_DATA_MAX_LENGTH) {
        ESP_LOGE(TAG, "Invalid data length or NULL pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Валидация JSON структуры
    if (!validate_command_json(data)) {
        ESP_LOGE(TAG, "Invalid JSON structure");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Парсинг JSON
    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON string");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Извлечение типа команды и данных
    const char* command_type = extract_command_type(data);
    const char* command_data = extract_command_data(data);

    if (command_type == NULL || command_data == NULL) {
        ESP_LOGE(TAG, "Missing 'type' or 'data' fields in JSON");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Валидация длины данных
    if (!validate_command_data_length(command_data)) {
        ESP_LOGE(TAG, "Command data too long");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Маршрутизация команды к соответствующему модулю
    esp_err_t result = ESP_OK;
    
    if (strcmp(command_type, "text") == 0) {
        // Текстовая команда
        ESP_LOGI(TAG, "Routing to text_commands module");
        result = execute_text_command(command_data);
    } else if (strcmp(command_type, "qr") == 0) {
        // QR команда
        ESP_LOGI(TAG, "Routing to qr_commands module");
        result = execute_qr_command(data); // Передаем весь JSON для доступа к цветам
    } else if (strcmp(command_type, "qr_text") == 0) {
        // QR+текст команда
        ESP_LOGI(TAG, "Routing to qr_text_commands module");
        result = execute_qr_text_command(data); // Передаем весь JSON для доступа к цветам и тексту
    } else if (strcmp(command_type, "clear") == 0) {
        // Команда очистки
        ESP_LOGI(TAG, "Routing to clear_commands module");
        result = execute_clear_command();
    } else {
        ESP_LOGE(TAG, "Unknown command type: %s", command_type);
        result = ESP_ERR_NOT_SUPPORTED;
    }

    cJSON_Delete(root);
    return result;
}

bool validate_command_json(const char *json_string) {
    if (json_string == NULL || strlen(json_string) == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        return false;
    }

    // Проверяем наличие обязательных полей
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *data_field = cJSON_GetObjectItemCaseSensitive(root, "data");

    bool is_valid = (cJSON_IsString(type) && cJSON_IsString(data_field));
    
    cJSON_Delete(root);
    return is_valid;
}

const char* extract_command_type(const char *json_string) {
    if (json_string == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        return NULL;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const char* result = NULL;
    
    if (cJSON_IsString(type)) {
        result = strdup(type->valuestring);
    }

    cJSON_Delete(root);
    return result;
}

const char* extract_command_data(const char *json_string) {
    if (json_string == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        return NULL;
    }

    cJSON *data_field = cJSON_GetObjectItemCaseSensitive(root, "data");
    const char* result = NULL;
    
    if (cJSON_IsString(data_field)) {
        result = strdup(data_field->valuestring);
    }

    cJSON_Delete(root);
    return result;
}

bool validate_command_data_length(const char *data) {
    if (data == NULL) {
        return false;
    }

    size_t data_len = strlen(data);
    
    // Для команды clear разрешаем пустой контент
    if (data_len == 0) {
        return true;
    }

    // Проверяем максимальную длину
    if (data_len > COMMAND_DATA_MAX_LENGTH) {
        ESP_LOGE(TAG, "Data too long: %zu bytes (max %d)", data_len, COMMAND_DATA_MAX_LENGTH);
        return false;
    }

    return true;
}
