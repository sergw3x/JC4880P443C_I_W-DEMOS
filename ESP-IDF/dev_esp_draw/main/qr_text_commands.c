#include "qr_text_commands.h"
#include "display_manager.h"
#include "lvgl_utils.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "qr_text_commands";

// Глобальные объекты для QR кода с текстом (определены для доступа из других модулей)
lv_obj_t *qr_text_qrcode_obj = NULL;
lv_obj_t *qr_text_label_obj = NULL;

// Forward declaration
static const lv_font_t* get_font_by_size(uint16_t font_size);

esp_err_t execute_qr_text_command(const char *json_string) {
    ESP_LOGI(TAG, "Executing QR+Text command with JSON: %s", json_string);
    
    if (json_string == NULL) {
        ESP_LOGE(TAG, "JSON string is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Парсим и извлекаем параметры
    qr_text_params_t params = {0};
    
    if (!parse_qr_text_json(json_string, &params)) {
        ESP_LOGE(TAG, "Failed to parse QR+Text JSON parameters");
        return ESP_ERR_INVALID_RESPONSE;
    }

#if LV_USE_QRCODE
    ESP_LOGI(TAG, "Creating QR code with text - QR: '%s', Text: '%s'", 
             params.qr_data ? params.qr_data : "NULL", 
             params.text ? params.text : "NULL");
    
    // Создаем QR код с текстом
    bool success = create_qr_with_text(&params);
    
    // Освобождаем память
    free_qr_text_params(&params);
    
    if (success) {
        ESP_LOGI(TAG, "QR code with text created successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to create QR code with text!");
        
        // Показываем сообщение об ошибке
        lv_obj_t *current_screen = get_current_screen();
        if (current_screen != NULL && get_lvgl_display() != NULL) {
            ESP_LOGI(TAG, "Showing error message");
            
            bool label_success = create_label_with_text("QR+Text Generation Failed", lv_color_white());
            if (label_success) {
                ESP_LOGI(TAG, "Error label created successfully");
            } else {
                ESP_LOGE(TAG, "Failed to create error label!");
            }
        }
        
        return ESP_FAIL;
    }
#else
    ESP_LOGI(TAG, "QR code support is disabled in this build");
    
    // Показываем сообщение об ошибке
    bool success = create_label_with_text("QR Code Support Disabled", lv_color_white());
    if (success) {
        ESP_LOGI(TAG, "QR disabled: Label created successfully");
    } else {
        ESP_LOGE(TAG, "QR disabled: Failed to create label!");
    }
    
    free_qr_text_params(&params);
    return ESP_OK;
#endif
}

bool create_qr_with_text(const qr_text_params_t *params) {
    if (params == NULL || get_current_screen() == NULL || get_lvgl_display() == NULL) {
        ESP_LOGE(TAG, "create_qr_with_text: Invalid parameters or LVGL not initialized");
        return false;
    }
    
    if (xSemaphoreTake(get_lvgl_mutex(), pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_qr_with_text: Failed to acquire mutex");
        return false;
    }
    
    ESP_LOGI(TAG, "create_qr_with_text: Starting");
    
    // Удаляем существующие объекты
    if (qr_text_qrcode_obj != NULL) {
        ESP_LOGI(TAG, "create_qr_with_text: Deleting existing QR object");
        if (is_lvgl_object_valid(qr_text_qrcode_obj)) {
            lv_obj_del(qr_text_qrcode_obj);
        }
        qr_text_qrcode_obj = NULL;
    }
    
    if (qr_text_label_obj != NULL) {
        ESP_LOGI(TAG, "create_qr_with_text: Deleting existing label object");
        if (is_lvgl_object_valid(qr_text_label_obj)) {
            lv_obj_del(qr_text_label_obj);
        }
        qr_text_label_obj = NULL;
    }
    
    // Очищаем экран и устанавливаем фон
    clear_screen_with_background(params->bg_color);
    
    // Получаем размеры экрана
    uint16_t screen_width = 480;   // LCD_H_RES
    uint16_t screen_height = 800;  // LCD_V_RES
    
    // Вычисляем размеры и позиции
    uint16_t qr_size = (uint16_t)(screen_width * 0.7);  // 70% ширины экрана
    uint16_t qr_y = screen_height * 0.2;                // QR код на 20% от верха
    uint16_t text_y = qr_y + qr_size + 30;              // Текст под QR кодом с отступом
    // uint16_t text_height = screen_height - text_y - 20; // Остаток высоты для текста (зарезервировано для будущего использования)
    (void)text_y; // Подавляем предупреждение о неиспользуемой переменной
    
    // Ограничиваем минимальные размеры
    if (qr_size > screen_width * 0.8) qr_size = (uint16_t)(screen_width * 0.8);
    if (qr_y < 10) qr_y = 10;
    
#if LV_USE_QRCODE
    // Создаем QR код
    ESP_LOGI(TAG, "create_qr_with_text: Creating QR code object");
    qr_text_qrcode_obj = lv_qrcode_create(get_current_screen(), qr_size, params->qr_color, params->bg_color);
    
    if (qr_text_qrcode_obj == NULL) {
        ESP_LOGE(TAG, "create_qr_with_text: Failed to create QR object!");
        xSemaphoreGive(get_lvgl_mutex());
        return false;
    }
    
    // Позиционируем QR код
    lv_obj_set_pos(qr_text_qrcode_obj, (screen_width - qr_size) / 2, qr_y);
    
    // Устанавливаем данные QR-кода
    if (params->qr_data != NULL && strlen(params->qr_data) > 0) {
        ESP_LOGI(TAG, "create_qr_with_text: Setting QR data to '%s'", params->qr_data);
        lv_res_t result = lv_qrcode_update(qr_text_qrcode_obj, params->qr_data, strlen(params->qr_data));
        
        if (result != LV_RES_OK) {
            ESP_LOGE(TAG, "create_qr_with_text: Failed to update QR data!");
            lv_obj_del(qr_text_qrcode_obj);
            qr_text_qrcode_obj = NULL;
            xSemaphoreGive(get_lvgl_mutex());
            return false;
        }
        
        ESP_LOGI(TAG, "create_qr_with_text: QR data updated successfully");
    }
#endif
    
    // Создаем текстовый объект
    if (params->text != NULL && strlen(params->text) > 0) {
        ESP_LOGI(TAG, "create_qr_with_text: Creating label object");
        
        qr_text_label_obj = lv_label_create(get_current_screen());
        if (qr_text_label_obj == NULL) {
            ESP_LOGE(TAG, "create_qr_with_text: Failed to create label object!");
#if LV_USE_QRCODE
            if (qr_text_qrcode_obj != NULL) {
                lv_obj_del(qr_text_qrcode_obj);
                qr_text_qrcode_obj = NULL;
            }
#endif
            xSemaphoreGive(get_lvgl_mutex());
            return false;
        }
        
        // Настраиваем текстовый объект
        lv_obj_set_width(qr_text_label_obj, screen_width - 40); // Отступы по бокам
        lv_obj_set_pos(qr_text_label_obj, 20, text_y);
        
        // Устанавливаем выравнивание и стиль
        lv_style_t label_style;
        lv_style_init(&label_style);
        lv_style_set_text_align(&label_style, LV_TEXT_ALIGN_CENTER);
        
        // Устанавливаем размер шрифта
        const lv_font_t* selected_font = get_font_by_size(params->font_size);
        lv_style_set_text_font(&label_style, selected_font);
        
        ESP_LOGI(TAG, "create_qr_with_text: Using font size %u (font: %p)", params->font_size, selected_font);
        
        lv_obj_add_style(qr_text_label_obj, &label_style, 0);
        
        // Устанавливаем цвет текста
        lv_obj_set_style_text_color(qr_text_label_obj, params->text_color, 0);
        
        // Устанавливаем текст
        lv_label_set_text(qr_text_label_obj, params->text);
        
        ESP_LOGI(TAG, "create_qr_with_text: Label created with text '%s'", params->text);
    }
    
    // Принудительно обновляем дисплей
    refresh_display();
    
    xSemaphoreGive(get_lvgl_mutex());
    ESP_LOGI(TAG, "create_qr_with_text: Completed successfully");
    
    return true;
}

void async_create_qr_with_text(void *data) {
    ESP_LOGW(TAG, "async_create_qr_with_text: Function not implemented");
    
    if (data != NULL) {
        qr_text_params_t *params = (qr_text_params_t *)data;
        free_qr_text_params(params);
        free(params);
    }
}

/**
 * @brief Выбирает шрифт LVGL по размеру
 * 
 * @param font_size Желаемый размер шрифта
 * @return const lv_font_t* Указатель на шрифт LVGL
 */
static const lv_font_t* get_font_by_size(uint16_t font_size) {
    // LVGL встроенные шрифты
    switch (font_size) {
        case 10: return &lv_font_montserrat_10;
        case 14: return &lv_font_montserrat_14;
        case 16: return &lv_font_montserrat_16;
        case 18: return &lv_font_montserrat_18;  // default
        case 20: return &lv_font_montserrat_20;
        case 24: return &lv_font_montserrat_24;
        case 28: return &lv_font_montserrat_28;
        case 32: return &lv_font_montserrat_32;
        default:
            // Автоматический выбор ближайшего доступного размера
            if (font_size < 14) return &lv_font_montserrat_10;
            if (font_size > 32) return &lv_font_montserrat_32;
            
            // Для промежуточных значений выбираем ближайший
            if (font_size <= 16) return &lv_font_montserrat_16;
            if (font_size <= 20) return &lv_font_montserrat_20;
            if (font_size <= 24) return &lv_font_montserrat_24;
            if (font_size <= 28) return &lv_font_montserrat_28;
            return &lv_font_montserrat_32;
    }
}

bool parse_qr_text_json(const char *json_string, qr_text_params_t *params) {
    if (json_string == NULL || params == NULL) {
        return false;
    }
    
    // Инициализируем параметры значениями по умолчанию
    memset(params, 0, sizeof(qr_text_params_t));
    params->qr_color = lv_color_black();
    params->bg_color = lv_color_white();
    params->text_color = lv_palette_main(LV_PALETTE_BLUE);
    params->font_size = 18; // Размер шрифта по умолчанию
    
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        ESP_LOGE(TAG, "parse_qr_text_json: Failed to parse JSON");
        return false;
    }
    
    // Извлекаем данные для QR кода
    cJSON *data_field = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsString(data_field) && data_field->valuestring != NULL) {
        params->qr_data = strdup(data_field->valuestring);
    } else {
        ESP_LOGE(TAG, "parse_qr_text_json: Missing or invalid 'data' field");
        cJSON_Delete(root);
        return false;
    }
    
    // Извлекаем текст (опционально)
    cJSON *text_field = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (cJSON_IsString(text_field) && text_field->valuestring != NULL) {
        params->text = strdup(text_field->valuestring);
    }
    
    // Извлекаем цвета (опционально)
    cJSON *qr_color_json = cJSON_GetObjectItemCaseSensitive(root, "qr_color");
    if (cJSON_IsString(qr_color_json)) {
        params->qr_color = parse_hex_color(qr_color_json->valuestring);
    }
    
    cJSON *bg_color_json = cJSON_GetObjectItemCaseSensitive(root, "bg_color");
    if (cJSON_IsString(bg_color_json)) {
        params->bg_color = parse_hex_color(bg_color_json->valuestring);
    }
    
    cJSON *text_color_json = cJSON_GetObjectItemCaseSensitive(root, "text_color");
    if (cJSON_IsString(text_color_json)) {
        params->text_color = parse_hex_color(text_color_json->valuestring);
    }
    
    // Извлекаем размер шрифта (опционально)
    cJSON *font_size_json = cJSON_GetObjectItemCaseSensitive(root, "font_size");
    if (cJSON_IsNumber(font_size_json)) {
        uint16_t requested_size = (uint16_t)font_size_json->valuedouble;
        
        // Валидация диапазона размера шрифта
        if (requested_size < 10) {
            ESP_LOGW(TAG, "parse_qr_text_json: Font size %u too small, using minimum 10", requested_size);
            params->font_size = 10;
        } else if (requested_size > 32) {
            ESP_LOGW(TAG, "parse_qr_text_json: Font size %u too large, using maximum 32", requested_size);
            params->font_size = 32;
        } else {
            params->font_size = requested_size;
        }
        
        ESP_LOGI(TAG, "parse_qr_text_json: Font size set to %u", params->font_size);
    } else {
        ESP_LOGI(TAG, "parse_qr_text_json: Using default font size 18");
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "parse_qr_text_json: Parsed successfully - QR data: '%s', Text: '%s'", 
             params->qr_data ? params->qr_data : "NULL", 
             params->text ? params->text : "NULL");
    
    return true;
}

void free_qr_text_params(qr_text_params_t *params) {
    if (params == NULL) {
        return;
    }
    
    if (params->qr_data != NULL) {
        free(params->qr_data);
        params->qr_data = NULL;
    }
    
    if (params->text != NULL) {
        free(params->text);
        params->text = NULL;
    }
}
