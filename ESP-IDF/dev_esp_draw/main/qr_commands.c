#include "qr_commands.h"
#include "display_manager.h"  // Для доступа к qrcode_obj
#include "lvgl_utils.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "qr_commands";

esp_err_t execute_qr_command(const char *json_string) {
    ESP_LOGI(TAG, "Executing QR command with JSON: %s", json_string);
    
    if (json_string == NULL) {
        ESP_LOGE(TAG, "JSON string is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Парсим JSON для извлечения данных и цветов
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON for QR command");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Извлекаем данные для QR кода
    cJSON *data_field = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsString(data_field) || data_field->valuestring == NULL) {
        ESP_LOGE(TAG, "Missing or invalid 'data' field in QR command");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    char *qr_data = strdup(data_field->valuestring);
    if (qr_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for QR data");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Парсим цвета из JSON (опциональные поля)
    lv_color_t qr_color = lv_color_black(); // По умолчанию черный
    lv_color_t screen_bg_color = lv_color_white(); // По умолчанию белый
    
    cJSON *qr_color_json = cJSON_GetObjectItemCaseSensitive(root, "qr_color");
    if (cJSON_IsString(qr_color_json)) {
        qr_color = parse_hex_color(qr_color_json->valuestring);
    }
    
    cJSON *screen_bg_color_json = cJSON_GetObjectItemCaseSensitive(root, "bg_color");
    if (cJSON_IsString(screen_bg_color_json)) {
        screen_bg_color = parse_hex_color(screen_bg_color_json->valuestring);
    }
    
    ESP_LOGI(TAG, "Using colors - QR: %s, Screen BG: %s", 
             cJSON_IsString(qr_color_json) ? qr_color_json->valuestring : "default",
             cJSON_IsString(screen_bg_color_json) ? screen_bg_color_json->valuestring : "default");

    cJSON_Delete(root);

#if LV_USE_QRCODE
    ESP_LOGI(TAG, "Creating QR code with data: '%s'", qr_data);
    
    // Создаем QR-код используя функцию из display_manager
    bool success = create_qr_code(qr_data, qr_color, screen_bg_color);
    
    if (success) {
        ESP_LOGI(TAG, "QR code created successfully");
        free(qr_data);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to create QR code!");
        
        // Показываем сообщение об ошибке
        lv_obj_t *current_screen = get_current_screen();
        if (current_screen != NULL && get_lvgl_display() != NULL) {
            ESP_LOGI(TAG, "Showing error message");
            
            bool label_success = create_label_with_text("QR Code Generation Failed", screen_bg_color);
            if (label_success) {
                ESP_LOGI(TAG, "Error label created successfully");
            } else {
                ESP_LOGE(TAG, "Failed to create error label!");
            }
        }
        
        free(qr_data);
        return ESP_FAIL;
    }
#else
    ESP_LOGI(TAG, "QR code support is disabled in this build");
    
    // Показываем сообщение об ошибке используя универсальную функцию
    ESP_LOGI(TAG, "QR disabled: Using unified function for label creation");
    
    bool success = create_label_with_text("QR Code Support Disabled", lv_color_white());
    if (success) {
        ESP_LOGI(TAG, "QR disabled: Label created successfully");
    } else {
        ESP_LOGE(TAG, "QR disabled: Failed to create label!");
    }
    
    free(qr_data);
    return ESP_OK;
#endif
}

bool create_qr_code(const char *data, lv_color_t qr_color, lv_color_t bg_color) {
    if (get_current_screen() == NULL || get_lvgl_display() == NULL) {
        ESP_LOGE(TAG, "create_qr_code: LVGL not initialized");
        return false;
    }
    
    if (xSemaphoreTake(get_lvgl_mutex(), pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_qr_code: Failed to acquire mutex");
        return false;
    }
    
    ESP_LOGI(TAG, "create_qr_code: Starting with data='%s'", data);
    ESP_LOGI(TAG, "create_qr_code: Current qrcode_obj state: %p", qrcode_obj);
    
    // Удаляем существующий QR объект безопасно ПЕРЕД очисткой экрана
    if (qrcode_obj != NULL) {
        ESP_LOGI(TAG, "create_qr_code: Deleting existing QR object at %p", qrcode_obj);
        
        if (is_lvgl_object_valid(qrcode_obj)) {
            ESP_LOGI(TAG, "create_qr_code: QR object is valid, proceeding with deletion");
            lv_obj_del(qrcode_obj);
        } else {
            ESP_LOGW(TAG, "create_qr_code: QR object is already invalid");
        }
        
        qrcode_obj = NULL;
        ESP_LOGI(TAG, "create_qr_code: QR object pointer set to NULL");
    }
    
    // Также удаляем QR+текст объекты если они существуют
    safe_qr_text_delete();
    
    // Очищаем экран и устанавливаем фон
    clear_screen_with_background(bg_color);
    
    // Вычисляем максимальный размер QR-кода (80% от меньшей стороны экрана)
    uint16_t screen_width = 480; // LCD_H_RES
    uint16_t screen_height = 800; // LCD_V_RES
    uint16_t max_size = (uint16_t)(MIN(screen_width, screen_height) * 0.8);
    
    // Обеспечиваем минимальный размер для читаемости
    if (max_size < 100) {
        max_size = MIN(screen_width, screen_height);
    }
    
    ESP_LOGI(TAG, "create_qr_code: Screen size: %dx%d, QR size: %d", 
             screen_width, screen_height, max_size);
    
    // Создаем новый QR объект
    ESP_LOGI(TAG, "create_qr_code: Creating new QR object");
    qrcode_obj = lv_qrcode_create(get_current_screen(), max_size, qr_color, bg_color);
    
    if (qrcode_obj == NULL) {
        ESP_LOGE(TAG, "create_qr_code: Failed to create QR object!");
        xSemaphoreGive(get_lvgl_mutex());
        return false;
    }
    
    ESP_LOGI(TAG, "create_qr_code: QR object created successfully at %p", qrcode_obj);
    
    // Центрируем QR-код на экране
    lv_obj_center(qrcode_obj);
    
    // Устанавливаем данные QR-кода
    if (data != NULL && strlen(data) > 0) {
        ESP_LOGI(TAG, "create_qr_code: Setting QR data to '%s'", data);
        lv_res_t result = lv_qrcode_update(qrcode_obj, data, strlen(data));
        
        if (result != LV_RES_OK) {
            ESP_LOGE(TAG, "create_qr_code: Failed to update QR data!");
            lv_obj_del(qrcode_obj);
            qrcode_obj = NULL;
            xSemaphoreGive(get_lvgl_mutex());
            return false;
        }
        
        ESP_LOGI(TAG, "create_qr_code: QR data updated successfully");
    } else {
        ESP_LOGW(TAG, "create_qr_code: Empty data provided for QR code");
    }
    
    // Принудительно обновляем дисплей
    refresh_display();
    
    xSemaphoreGive(get_lvgl_mutex());
    ESP_LOGI(TAG, "create_qr_code: Completed successfully");
    
    return true;
}

void async_draw_qr(void *data) {
    ESP_LOGW(TAG, "QR functionality is not implemented");
    
    if (data != NULL) {
        free(data);
    }
}
