#include "text_commands.h"
#include "display_manager.h"
#include "lvgl_utils.h"
#include "esp_log.h"

static const char *TAG = "text_commands";

// Глобальные переменные из display_manager
extern lv_obj_t *label_obj;
static lv_style_t label_style;

esp_err_t execute_text_command(const char *text_data) {
    ESP_LOGI(TAG, "Executing text command with data: '%s'", text_data);
    
    if (text_data == NULL) {
        ESP_LOGE(TAG, "Text data is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Очищаем экран перед отображением текста
    ESP_LOGI(TAG, "Clearing screen before text display");
    
    lv_obj_t *current_screen = get_current_screen();
    if (current_screen != NULL && get_lvgl_display() != NULL) {
        // Берем мьютекс с таймаутом
        if (xSemaphoreTake(get_lvgl_mutex(), pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "Mutex acquired for text command");
            
            clear_screen_with_background(lv_color_white());
            
            xSemaphoreGive(get_lvgl_mutex());
            ESP_LOGI(TAG, "Mutex released after screen clear");
        } else {
            ESP_LOGE(TAG, "Failed to acquire mutex for text command");
            return ESP_ERR_TIMEOUT;
        }
        
        // Вызываем display_text_lvgl без мьютекса (он сам его возьмет)
        display_text_lvgl(text_data);
        
        // Принудительно обновляем дисплей
        if (get_lvgl_display()) {
            ESP_LOGI(TAG, "Triggering display refresh for text");
            refresh_display();
        }
    } else {
        ESP_LOGW(TAG, "LVGL not initialized, cannot display text");
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

void display_text_lvgl(const char *text) {
    const char* local_TAG = "display_text";
    
    if (text == NULL) {
        ESP_LOGE(TAG,"%s: Text is NULL", local_TAG);
        return;
    }
    
    ESP_LOGI(TAG,"%s: Called with text: '%s'", local_TAG, text);
    
    // Используем универсальную функцию для создания label
    bool success = create_label_with_text(text, lv_color_white());
    
    if (success) {
        ESP_LOGI(TAG,"%s: Text display completed successfully", local_TAG);
    } else {
        ESP_LOGE(TAG,"%s: Failed to display text", local_TAG);
    }
}

void async_display_text(void *data) {
    char *local_TAG = "async_display_text";
    char *text_data = (char *) data;

    ESP_LOGI(TAG,"%s: Starting text display", local_TAG);

    if (text_data != NULL) {
        display_text_lvgl(text_data);
        
        // Принудительно обновляем дисплей
        if (get_lvgl_display() != NULL) {
            refresh_display();
        }
    }

    ESP_LOGI(TAG,"%s: Finished text display", local_TAG);

    free(text_data);
}

void lv_label_hide(){
    if (get_lvgl_mutex() == NULL) {
        ESP_LOGW(TAG, "Cannot hide label: LVGL mutex not initialized");
        return;
    }
    
    if (xSemaphoreTake(get_lvgl_mutex(), pdMS_TO_TICKS(100)) == pdTRUE) {
        if (label_obj != NULL) {
            lv_obj_set_visibility(label_obj, false, "label");
        } else {
            ESP_LOGW(TAG, "Cannot hide label: object is NULL");
        }
        xSemaphoreGive(get_lvgl_mutex());
    } else {
        ESP_LOGW(TAG, "Failed to acquire mutex for label hide operation");
    }
}

void lv_label_show(){
    if (get_lvgl_mutex() == NULL) {
        ESP_LOGW(TAG, "Cannot show label: LVGL mutex not initialized");
        return;
    }
    
    if (xSemaphoreTake(get_lvgl_mutex(), pdMS_TO_TICKS(100)) == pdTRUE) {
        if (label_obj != NULL) {
            lv_obj_set_visibility(label_obj, true, "label");
        } else {
            ESP_LOGW(TAG, "Cannot show label: object is NULL");
        }
        xSemaphoreGive(get_lvgl_mutex());
    } else {
        ESP_LOGW(TAG, "Failed to acquire mutex for label show operation");
    }
}

