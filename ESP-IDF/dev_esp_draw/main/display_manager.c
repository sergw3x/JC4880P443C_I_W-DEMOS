#include "display_manager.h"
#include "lvgl_utils.h"
#include "esp_log.h"

static const char *TAG = "display_manager";

// Глобальные переменные LVGL (перенесены из main.c)
lv_obj_t *label_obj = NULL;
#if LV_USE_QRCODE
    lv_obj_t *qrcode_obj = NULL;
#endif
static SemaphoreHandle_t lvgl_mutex = NULL;
static lv_disp_t *lvgl_disp = NULL;

// Глобальная переменная панели (из hardware_manager)
extern esp_lcd_panel_handle_t disp_panel;

bool create_label_with_text(const char *text, lv_color_t bg_color) {
    if (get_current_screen() == NULL || get_lvgl_display() == NULL) {
        ESP_LOGE(TAG, "create_label_with_text: LVGL not initialized");
        return false;
    }
    
    if (xSemaphoreTake(get_lvgl_mutex(), pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_label_with_text: Failed to acquire mutex");
        return false;
    }
    
    ESP_LOGI(TAG, "create_label_with_text: Starting with text='%s'", text);
    ESP_LOGI(TAG, "create_label_with_text: Current label_obj state: %p", label_obj);
    
    // Удаляем существующие объекты безопасно ПЕРЕД очисткой экрана
    safe_label_delete();
    
#if LV_USE_QRCODE
    // Также удаляем QR объект если он существует
    safe_qrcode_delete();
#endif
    
    // Очищаем экран и устанавливаем фон
    clear_screen_with_background(bg_color);
    
    // Создаем новый объект
    ESP_LOGI(TAG, "create_label_with_text: Creating new label object");
    label_obj = lv_label_create(get_current_screen());
    
    if (label_obj == NULL) {
        ESP_LOGE(TAG, "create_label_with_text: Failed to create label object!");
        xSemaphoreGive(get_lvgl_mutex());
        return false;
    }
    
    ESP_LOGI(TAG, "create_label_with_text: Label object created successfully at %p", label_obj);
    
    // Настраиваем объект
    lv_obj_set_width(label_obj, LV_PCT(90));
    lv_obj_center(label_obj);
    
    // Устанавливаем стили (initialized once in app_main)
    lv_style_t label_style_local;
    lv_style_init(&label_style_local);
    lv_style_set_text_align(&label_style_local, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_style(label_obj, &label_style_local, 0);
    
    // Устанавливаем цвет текста
    lv_color_t text_color = lv_palette_main(LV_PALETTE_BLUE);
    lv_obj_set_style_text_color(label_obj, text_color, 0);

    if (text == NULL) text = "";

    ESP_LOGI(TAG,"create_label_with_text: Setting text to '%s'", text);
    
    if (strlen(text) > 0) {
        lv_label_set_text(label_obj, text);
    } else {
        lv_label_set_text(label_obj, " ");
    }
    
    // Принудительно обновляем дисплей
    refresh_display();
    
    xSemaphoreGive(get_lvgl_mutex());
    ESP_LOGI(TAG, "create_label_with_text: Completed successfully");
    
    return true;
}

void lv_obj_set_visibility(lv_obj_t *obj, bool visible, const char* obj_name) {
    if (obj != NULL) {
        if (visible) {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "%s show", obj_name);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "%s hide", obj_name);
        }
    } else {
        ESP_LOGW(TAG, "Cannot %s %s: object is NULL", visible ? "show" : "hide", obj_name);
    }
}

void safe_label_delete(void) {
    if (label_obj != NULL) {
        ESP_LOGI(TAG, "safe_label_delete: Deleting label object at %p", label_obj);
        
        // Проверяем, что объект еще действителен
        if (is_lvgl_object_valid(label_obj)) {
            ESP_LOGI(TAG, "safe_label_delete: Object is valid, proceeding with deletion");
            lv_obj_del(label_obj);
        } else {
            ESP_LOGW(TAG, "safe_label_delete: Object is already invalid (possibly auto-deleted by LVGL)");
        }
        
        label_obj = NULL;
        ESP_LOGI(TAG, "safe_label_delete: Label object pointer set to NULL");
    } else {
        ESP_LOGI(TAG, "safe_label_delete: No label object to delete");
    }
}

#if LV_USE_QRCODE
void safe_qrcode_delete(void) {
    if (qrcode_obj != NULL) {
        ESP_LOGI(TAG, "safe_qrcode_delete: Deleting QR object at %p", qrcode_obj);
        
        // Проверяем, что объект еще действителен
        if (is_lvgl_object_valid(qrcode_obj)) {
            ESP_LOGI(TAG, "safe_qrcode_delete: QR object is valid, proceeding with deletion");
            lv_obj_del(qrcode_obj);
        } else {
            ESP_LOGW(TAG, "safe_qrcode_delete: QR object is already invalid (possibly auto-deleted by LVGL)");
        }
        
        qrcode_obj = NULL;
        ESP_LOGI(TAG, "safe_qrcode_delete: QR object pointer set to NULL");
    } else {
        ESP_LOGI(TAG, "safe_qrcode_delete: No QR object to delete");
    }
}
#endif

void refresh_display(void) {
    if (get_lvgl_display() != NULL) {
        lv_refr_now(get_lvgl_display());
    }
}

SemaphoreHandle_t get_lvgl_mutex(void) {
    return lvgl_mutex;
}

lv_disp_t* get_lvgl_display(void) {
    return lvgl_disp;
}

// Функции для установки глобальных переменных
void set_lvgl_mutex(SemaphoreHandle_t mutex) {
    lvgl_mutex = mutex;
}

void set_lvgl_display(lv_disp_t *disp) {
    lvgl_disp = disp;
}





esp_lcd_panel_handle_t get_display_panel(void) {
    return disp_panel;
}

void init_label_styles(void) {
    // Инициализируем стили для label объектов
    static lv_style_t label_style;
    
    // Инициализируем стиль только один раз
    static bool style_initialized = false;
    
    if (!style_initialized) {
        lv_style_init(&label_style);
        lv_style_set_text_align(&label_style, LV_TEXT_ALIGN_CENTER);
        style_initialized = true;
    }
}