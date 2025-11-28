#include "clear_commands.h"
#include "display_manager.h"  // Для доступа к label_obj и qrcode_obj
#include "lvgl_utils.h"
#include "text_commands.h"
#include "qr_commands.h"
#if LV_USE_QRCODE
#include "qr_text_commands.h"
#endif
#include "esp_log.h"

static const char *TAG = "clear_commands";

esp_err_t execute_clear_command(void) {
    ESP_LOGI(TAG, "Executing clear screen command");
    
    // Проверяем инициализацию LVGL
    lv_obj_t *current_screen = get_current_screen();
    if (current_screen == NULL || get_lvgl_display() == NULL) {
        ESP_LOGW(TAG, "LVGL not initialized, cannot clear screen");
        return ESP_ERR_INVALID_STATE;
    }

    // Берем мьютекс для синхронизации
    if (xSemaphoreTake(get_lvgl_mutex(), portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "Cleaning screen objects");
        
        // Очищаем все объекты на экране
        lv_obj_clean(current_screen);
        
        ESP_LOGI(TAG, "Setting white background");
        lv_obj_set_style_bg_color(current_screen, lv_color_white(), 0);
        
        // Сбрасываем глобальные указатели объектов
        label_obj = NULL;
        
#if LV_USE_QRCODE
        qrcode_obj = NULL;
        qr_text_qrcode_obj = NULL;
        qr_text_label_obj = NULL;
#endif
        
        // Принудительно обновляем дисплей
        if (get_lvgl_display()) {
            ESP_LOGI(TAG, "Triggering display refresh for clear");
            refresh_display();
        }
        
        xSemaphoreGive(get_lvgl_mutex());
    } else {
        ESP_LOGE(TAG, "Failed to acquire mutex for clear command");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "Screen cleared successfully");
    return ESP_OK;
}
