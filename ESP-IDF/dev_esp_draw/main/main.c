#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_st7701.h"

#include <string.h>
#include "cJSON.h"
#include "lvgl.h"

#if LV_USE_QRCODE
    #include "extra/libs/qrcode/lv_qrcode.h"
#endif

// LCD Hardware Configuration
#define LCD_BACKLIGHT                              (GPIO_NUM_23)
#define LCD_RST                                    (GPIO_NUM_5)

// Разрешение экрана - перемещено выше для использования в функциях
#define LCD_H_RES                                  (480)       // Horizontal resolution in pixels  
#define LCD_V_RES                                  (800)       // Vertical resolution in pixels

// Глобальные переменные LVGL
static lv_obj_t *label_obj = NULL;
static lv_style_t label_style;
#if LV_USE_QRCODE
    static lv_obj_t *qrcode_obj = NULL;
#endif
static SemaphoreHandle_t lvgl_mutex = NULL;

// Глобальные переменные дисплея
static const char *TAG                           = "esp_draw_bit";
const uint16_t  white_color                      = 0xFFFF;
const uint16_t black_color                       = 0x0000;
static         esp_lcd_panel_handle_t disp_panel = NULL;
static         lv_disp_draw_buf_t     disp_buf;
static         lv_disp_t              *lvgl_disp = NULL;
static         lv_color_t             *buf1 = NULL;
static         lv_color_t             *buf2 = NULL;


// Безопасная функция для удаления label объекта с проверкой мьютекса
static void safe_label_delete(void) {
    if (label_obj != NULL) {
        ESP_LOGI(TAG, "safe_label_delete: Deleting label object at %p", label_obj);
        
        // Проверяем, что объект еще действителен
        if (lv_obj_is_valid(label_obj)) {
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
// Безопасная функция для удаления QR объекта с проверкой мьютекса
static void safe_qrcode_delete(void) {
    if (qrcode_obj != NULL) {
        ESP_LOGI(TAG, "safe_qrcode_delete: Deleting QR object at %p", qrcode_obj);
        
        // Проверяем, что объект еще действителен
        if (lv_obj_is_valid(qrcode_obj)) {
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

// Универсальная функция для создания и настройки label объекта
static bool create_label_with_text(const char *text, lv_color_t bg_color) {
    if (lv_scr_act() == NULL || lvgl_disp == NULL) {
        ESP_LOGE(TAG, "create_label_with_text: LVGL not initialized");
        return false;
    }
    
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
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
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), bg_color, 0);
    
    // Создаем новый объект
    ESP_LOGI(TAG, "create_label_with_text: Creating new label object");
    label_obj = lv_label_create(lv_scr_act());
    
    if (label_obj == NULL) {
        ESP_LOGE(TAG, "create_label_with_text: Failed to create label object!");
        xSemaphoreGive(lvgl_mutex);
        return false;
    }
    
    ESP_LOGI(TAG, "create_label_with_text: Label object created successfully at %p", label_obj);
    
    // Настраиваем объект
    lv_obj_set_width(label_obj, LV_PCT(90));
    lv_obj_center(label_obj);
    
    // Устанавливаем стили (initialized once in app_main)
    lv_obj_add_style(label_obj, &label_style, 0);
    
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
    lv_refr_now(lvgl_disp);
    
    xSemaphoreGive(lvgl_mutex);
    ESP_LOGI(TAG, "create_label_with_text: Completed successfully");
    
    return true;
}

// Глобальные переменные для QR+Text (определены здесь для использования в main.c)
#if LV_USE_QRCODE
static lv_obj_t *qr_text_qrcode_obj = NULL;
static lv_obj_t *qr_text_label_obj = NULL;
#endif

/**
 * @brief Выбирает шрифт LVGL по размеру (локальная версия для main.c)
 * 
 * @param font_size Желаемый размер шрифта
 * @return const lv_font_t* Указатель на шрифт LVGL
 */
static const lv_font_t* get_font_by_size_simple(uint16_t font_size) {
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

// Forward declaration for text formatting function
static char* process_text_formatting_simple(const char *text);

// Функция для создания QR кода с текстом (упрощенная версия для main.c)
static bool create_qr_with_text_simple(const char *qr_data, const char *text_data, lv_color_t qr_color, lv_color_t bg_color, lv_color_t text_color, uint16_t font_size, lv_text_align_t text_align) {
    if (lv_scr_act() == NULL || lvgl_disp == NULL) {
        ESP_LOGE(TAG, "create_qr_with_text_simple: LVGL not initialized");
        return false;
    }
    
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_qr_with_text_simple: Failed to acquire mutex");
        return false;
    }
    
    ESP_LOGI(TAG, "create_qr_with_text_simple: Starting");
    ESP_LOGI(TAG, "QR data: '%s', Text: '%s'", qr_data ? qr_data : "NULL", text_data ? text_data : "NULL");
    
    // Удаляем существующие объекты
    if (qr_text_qrcode_obj != NULL) {
        ESP_LOGI(TAG, "create_qr_with_text_simple: Deleting existing QR object");
        if (lv_obj_is_valid(qr_text_qrcode_obj)) {
            lv_obj_del(qr_text_qrcode_obj);
        }
        qr_text_qrcode_obj = NULL;
    }
    
    if (qr_text_label_obj != NULL) {
        ESP_LOGI(TAG, "create_qr_with_text_simple: Deleting existing label object");
        if (lv_obj_is_valid(qr_text_label_obj)) {
            lv_obj_del(qr_text_label_obj);
        }
        qr_text_label_obj = NULL;
    }
    
    // Очищаем экран и устанавливаем фон
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), bg_color, 0);
    
    // Получаем размеры экрана
    uint16_t screen_width = LCD_H_RES;
    uint16_t screen_height = LCD_V_RES;
    
    // Вычисляем размеры и позиции
    uint16_t qr_size = (uint16_t)(screen_width * 0.7);  // 70% ширины экрана
    uint16_t qr_y = screen_height * 0.2;                // QR код на 20% от верха
    uint16_t text_y = qr_y + qr_size + 30;              // Текст под QR кодом с отступом
    
    // Ограничиваем минимальные размеры
    if (qr_size > screen_width * 0.8) qr_size = (uint16_t)(screen_width * 0.8);
    if (qr_y < 10) qr_y = 10;
    
#if LV_USE_QRCODE
    // Создаем QR код
    ESP_LOGI(TAG, "create_qr_with_text_simple: Creating QR code object");
    qr_text_qrcode_obj = lv_qrcode_create(lv_scr_act(), qr_size, qr_color, bg_color);
    
    if (qr_text_qrcode_obj == NULL) {
        ESP_LOGE(TAG, "create_qr_with_text_simple: Failed to create QR object!");
        xSemaphoreGive(lvgl_mutex);
        return false;
    }
    
    // Позиционируем QR код
    lv_obj_set_pos(qr_text_qrcode_obj, (screen_width - qr_size) / 2, qr_y);
    
    // Устанавливаем данные QR-кода
    if (qr_data != NULL && strlen(qr_data) > 0) {
        ESP_LOGI(TAG, "create_qr_with_text_simple: Setting QR data to '%s'", qr_data);
        lv_res_t result = lv_qrcode_update(qr_text_qrcode_obj, qr_data, strlen(qr_data));
        
        if (result != LV_RES_OK) {
            ESP_LOGE(TAG, "create_qr_with_text_simple: Failed to update QR data!");
            lv_obj_del(qr_text_qrcode_obj);
            qr_text_qrcode_obj = NULL;
            xSemaphoreGive(lvgl_mutex);
            return false;
        }
        
        ESP_LOGI(TAG, "create_qr_with_text_simple: QR data updated successfully");
    }
#endif
    
    // Создаем текстовый объект
    if (text_data != NULL && strlen(text_data) > 0) {
        ESP_LOGI(TAG, "create_qr_with_text_simple: Creating label object");
        
        qr_text_label_obj = lv_label_create(lv_scr_act());
        if (qr_text_label_obj == NULL) {
            ESP_LOGE(TAG, "create_qr_with_text_simple: Failed to create label object!");
#if LV_USE_QRCODE
            if (qr_text_qrcode_obj != NULL) {
                lv_obj_del(qr_text_qrcode_obj);
                qr_text_qrcode_obj = NULL;
            }
#endif
            xSemaphoreGive(lvgl_mutex);
            return false;
        }
        
        // Настраиваем текстовый объект
        lv_obj_set_width(qr_text_label_obj, screen_width - 40); // Отступы по бокам
        lv_obj_set_pos(qr_text_label_obj, 20, text_y);
        
        // Устанавливаем выравнивание и стиль
        lv_style_t label_style;
        lv_style_init(&label_style);
        lv_style_set_text_align(&label_style, text_align);
        
        // Устанавливаем размер шрифта
        const lv_font_t* selected_font = get_font_by_size_simple(font_size);
        lv_style_set_text_font(&label_style, selected_font);
        
        ESP_LOGI(TAG, "create_qr_with_text_simple: Using font size %u (font: %p)", font_size, selected_font);
        
        lv_obj_add_style(qr_text_label_obj, &label_style, 0);
        
        // Устанавливаем цвет текста
        lv_obj_set_style_text_color(qr_text_label_obj, text_color, 0);
        
        // Обрабатываем текст (заменяем {newline} на реальные переносы строк)
        char *processed_text = process_text_formatting_simple(text_data);
        
        // Устанавливаем обработанный текст
        lv_label_set_text(qr_text_label_obj, processed_text ? processed_text : text_data);
        
        // Освобождаем память обработанного текста
        if (processed_text != NULL) {
            free(processed_text);
        }
        
        ESP_LOGI(TAG, "create_qr_with_text_simple: Label created with text '%s'", text_data);
    }
    
    // Принудительно обновляем дисплей
    lv_refr_now(lvgl_disp);
    
    xSemaphoreGive(lvgl_mutex);
    ESP_LOGI(TAG, "create_qr_with_text_simple: Completed successfully");
    
    return true;
}

// Функция для создания QR-кода с максимальным размером 80% экрана
static bool create_qr_code(const char *data, lv_color_t qr_color, lv_color_t bg_color) {
    if (lv_scr_act() == NULL || lvgl_disp == NULL) {
        ESP_LOGE(TAG, "create_qr_code: LVGL not initialized");
        return false;
    }
    
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_qr_code: Failed to acquire mutex");
        return false;
    }
    
    ESP_LOGI(TAG, "create_qr_code: Starting with data='%s'", data);
    ESP_LOGI(TAG, "create_qr_code: Current qrcode_obj state: %p", qrcode_obj);
    
    // Удаляем существующий QR объект безопасно ПЕРЕД очисткой экрана
    if (qrcode_obj != NULL) {
        ESP_LOGI(TAG, "create_qr_code: Deleting existing QR object at %p", qrcode_obj);
        
        if (lv_obj_is_valid(qrcode_obj)) {
            ESP_LOGI(TAG, "create_qr_code: QR object is valid, proceeding with deletion");
            lv_obj_del(qrcode_obj);
        } else {
            ESP_LOGW(TAG, "create_qr_code: QR object is already invalid");
        }
        
        qrcode_obj = NULL;
        ESP_LOGI(TAG, "create_qr_code: QR object pointer set to NULL");
    }
    
    // Очищаем экран и устанавливаем фон
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), bg_color, 0);
    
    // Вычисляем максимальный размер QR-кода (80% от меньшей стороны экрана)
    uint16_t screen_width = LCD_H_RES;
    uint16_t screen_height = LCD_V_RES;
    uint16_t max_size = (uint16_t)(MIN(screen_width, screen_height) * 0.8);
    
    // Обеспечиваем минимальный размер для читаемости
    if (max_size < 100) {
        max_size = MIN(screen_width, screen_height);
    }
    
    ESP_LOGI(TAG, "create_qr_code: Screen size: %dx%d, QR size: %d", 
             screen_width, screen_height, max_size);
    
    // Создаем новый QR объект
    ESP_LOGI(TAG, "create_qr_code: Creating new QR object");
    qrcode_obj = lv_qrcode_create(lv_scr_act(), max_size, qr_color, bg_color);
    
    if (qrcode_obj == NULL) {
        ESP_LOGE(TAG, "create_qr_code: Failed to create QR object!");
        xSemaphoreGive(lvgl_mutex);
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
            xSemaphoreGive(lvgl_mutex);
            return false;
        }
        
        ESP_LOGI(TAG, "create_qr_code: QR data updated successfully");
    } else {
        ESP_LOGW(TAG, "create_qr_code: Empty data provided for QR code");
    }
    
    // Принудительно обновляем дисплей
    lv_refr_now(lvgl_disp);
    
    xSemaphoreGive(lvgl_mutex);
    ESP_LOGI(TAG, "create_qr_code: Completed successfully");
    
    return true;
}

// Функция для парсинга hex цвета в формате "#RRGGBB" или "RRGGBB"
static lv_color_t parse_hex_color(const char *color_str) {
    if (color_str == NULL || strlen(color_str) < 6) {
        ESP_LOGW("parse_hex_color", "Invalid color string: %s", color_str ? color_str : "NULL");
        return lv_color_black(); // Возвращаем черный цвет по умолчанию
    }
    
    // Убираем символ # если он есть
    const char *hex_start = color_str;
    if (color_str[0] == '#') {
        hex_start = color_str + 1;
    }
    
    // Проверяем длину
    if (strlen(hex_start) != 6) {
        ESP_LOGW("parse_hex_color", "Invalid hex color format: %s (expected RRGGBB)", color_str);
        return lv_color_black();
    }
    
    // Парсим RGB компоненты
    char r_str[3] = {hex_start[0], hex_start[1], '\0'};
    char g_str[3] = {hex_start[2], hex_start[3], '\0'};
    char b_str[3] = {hex_start[4], hex_start[5], '\0'};
    
    uint8_t r = (uint8_t)strtol(r_str, NULL, 16);
    uint8_t g = (uint8_t)strtol(g_str, NULL, 16);
    uint8_t b = (uint8_t)strtol(b_str, NULL, 16);
    
    ESP_LOGI("parse_hex_color", "Parsed color %s -> RGB(%d,%d,%d)", color_str, r, g, b);
    
    return lv_color_make(r, g, b);
}

/**
 * @brief Обрабатывает текст, заменяя маркеры {newline} на реальные переносы строк (упрощенная версия для main.c)
 * 
 * @param text Исходный текст с маркерами {newline}
 * @return char* Обработанный текст с переносами строк (нужно освободить память)
 */
static char* process_text_formatting_simple(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    
    // Подсчитываем количество маркеров {newline}
    size_t newline_count = 0;
    const char *pos = text;
    while ((pos = strstr(pos, "{newline}")) != NULL) {
        newline_count++;
        pos += 9; // Длина строки "{newline}"
    }
    
    // Если маркеров нет, возвращаем копию исходного текста
    if (newline_count == 0) {
        return strdup(text);
    }
    
    // Вычисляем новую длину строки
    size_t original_len = strlen(text);
    size_t new_len = original_len - (newline_count * 9) + newline_count; // Заменяем "{newline}" на "\n"
    
    // Выделяем память для новой строки
    char *processed_text = malloc(new_len + 1);
    if (processed_text == NULL) {
        ESP_LOGE(TAG, "process_text_formatting_simple: Failed to allocate memory for processed text");
        return NULL;
    }
    
    // Копируем и заменяем маркеры
    const char *src = text;
    char *dst = processed_text;
    
    while (*src != '\0') {
        if (strncmp(src, "{newline}", 9) == 0) {
            *dst++ = '\n';
            src += 9;
        } else {
            *dst++ = *src++;
        }
    }
    
    *dst = '\0';
    
    ESP_LOGI(TAG, "process_text_formatting_simple: Processed text with %zu newlines", newline_count);
    
    return processed_text;
}

// LCD Hardware Configuration
#define LCD_BACKLIGHT                              (GPIO_NUM_23)
#define LCD_RST                                    (GPIO_NUM_5)

// MIPI DSI Configuration
#define BSP_LCD_MIPI_DSI_LANE_NUM                  (2)         // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS         (1500)      // 1Gbps
#define LANE_BITRATE_MBPS                          500         // Lane bit rate in Mbps

// Power Management
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN              (3)         // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV        (2500)      // LDO voltage in millivolts

// LVGL Configuration
#define V_TASK_DELAY                               10          // LVGL task delay in milliseconds
#define LVGL_BUFFER_SIZE                           (LCD_H_RES * LCD_V_RES)
#define LVGL_BUFFER_FACTOR                         4           // Buffer size = screen_size / factor

// Backlight Configuration
#define USE_PWM_BACKLIGHT                          1           // Use PWM for backlight control (0=off, 1=on)
#define BACKLIGHT_LEVEL                            20          // Backlight brightness level (0-100%)
#define LEDC_TIMER                                 LEDC_TIMER_0
#define LEDC_MODE                                  LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL                               LEDC_CHANNEL_0
#define LEDC_DUTY_RESOLUTION                       LEDC_TIMER_10_BIT  // 10-bit resolution (0-1023)
#define LEDC_FREQUENCY                             (25000)     // PWM frequency in Hz (25 KHz)

// Communication Configuration
#define USB_RX_BUFFER_SIZE                         256         // USB input buffer size in bytes

// QR Code Configuration (disabled)
#define QR_SIZE                                    ((uint16_t)(LCD_V_RES * 0.56)) // 56% of screen height for optimal size
#define QR_CONTENT_MAX_LENGTH                      (512)       // Maximum QR content length in bytes

// Конфигурационная структура приложения
typedef struct {
    // LCD параметры
    uint16_t lcd_h_res;
    uint16_t lcd_v_res;
    
    // Подсветка
    bool use_pwm_backlight;
    uint8_t backlight_level; // 0-100%
    
    // LVGL параметры
    uint32_t lvgl_buffer_factor; // делитель для размера буфера (4 = 1/4 экрана)
    
    // Производительность
    uint32_t lvgl_task_delay_ms;
} app_config_t;

static const app_config_t config = {
    .lcd_h_res = LCD_H_RES,
    .lcd_v_res = LCD_V_RES,
    .use_pwm_backlight = USE_PWM_BACKLIGHT,
    .backlight_level = BACKLIGHT_LEVEL,
    .lvgl_buffer_factor = LVGL_BUFFER_FACTOR, // буфер = 1/4 от размера экрана
    .lvgl_task_delay_ms = V_TASK_DELAY,
};

#define ST7701_480_800_PANEL_60HZ_DPI_CONFIG(px_format)  \
    {                                                    \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,     \
        .dpi_clock_freq_mhz    = 34,                     \
        .virtual_channel       = 0,                      \
        .pixel_format          = px_format,              \
        .num_fbs               = 1,                      \
        .video_timing = {                                \
            .h_size            = 480,                    \
            .v_size            = 800,                    \
            .hsync_back_porch  = 42,                     \
            .hsync_pulse_width = 12,                     \
            .hsync_front_porch = 42,                     \
            .vsync_back_porch  = 8,                      \
            .vsync_pulse_width = 2,                      \
            .vsync_front_porch = 166,                    \
        },                                               \
        .flags.use_dma2d       = true,                   \
    }



IRAM_ATTR static bool test_notify_refresh_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
    lv_disp_drv_t *drv       = (lv_disp_drv_t *)user_ctx;
    BaseType_t    need_yield = pdFALSE;

    ESP_EARLY_LOGI(TAG, "DMA done, calling lv_disp_flush_ready");

    lv_disp_flush_ready(drv);
    return (need_yield == pdTRUE);
}

static esp_err_t bsp_enable_dsi_phy_power(void) {
    #if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
                .chan_id    = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
                .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, "Acquire LDO channel for DPHY failed");
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
    #endif

    return ESP_OK;
}

void init_backlight(void) {
    esp_err_t ret;
    
    if (config.use_pwm_backlight == false) {
        ESP_LOGI(TAG, "Turn on LCD backlight without PWM");
        
        // Configure GPIO as output
        gpio_config_t bk_gpio_config = {
                .mode         = GPIO_MODE_OUTPUT,
                .pin_bit_mask = 1ULL << LCD_BACKLIGHT
        };
        
        ret = gpio_config(&bk_gpio_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure backlight GPIO: %s", esp_err_to_name(ret));
            return;
        }
        
        // turn on backlight 100% w/o pwm
        ret = gpio_set_level(LCD_BACKLIGHT, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set backlight level: %s", esp_err_to_name(ret));
        }
        
        return;
    }

    ESP_LOGI(TAG, "Turn on LCD backlight using PWM");
    
    // Configure GPIO as output
    gpio_config_t bk_gpio_config = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << LCD_BACKLIGHT
    };
    
    ret = gpio_config(&bk_gpio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure backlight GPIO: %s", esp_err_to_name(ret));
        return;
    }

    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
            .speed_mode       = LEDC_MODE,
            .duty_resolution  = LEDC_DUTY_RESOLUTION,
            .timer_num        = LEDC_TIMER,
            .freq_hz          = LEDC_FREQUENCY,
            .clk_cfg          = LEDC_AUTO_CLK,
    };
    
    ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return;
    }

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CHANNEL,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = LCD_BACKLIGHT,
            .duty           = 0,
            .hpoint         = 0,
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return;
    }

    // Set PWM duty cycle
    uint32_t duty = (((1 << LEDC_DUTY_RESOLUTION) - 1) / 100) * config.backlight_level;
    ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty cycle: %s", esp_err_to_name(ret));
        return;
    }

    ret = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDC duty cycle: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Backlight initialized successfully at %d%% brightness", config.backlight_level);
}

void init_lcd(void) {
    esp_err_t ret;
    
    // Enable DSI PHY power
    ret = bsp_enable_dsi_phy_power();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable DSI PHY power: %s", esp_err_to_name(ret));
        goto lcd_init_error;
    }

    // Create DSI bus
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id             = 0,
            .num_data_lanes     = BSP_LCD_MIPI_DSI_LANE_NUM,
            .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = LANE_BITRATE_MBPS,
    };
    
    ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DSI bus: %s", esp_err_to_name(ret));
        goto lcd_init_error;
    }

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    
    // Create panel IO
    esp_lcd_panel_io_handle_t   io;
    esp_lcd_dbi_io_config_t     dbi_config = {
            .virtual_channel    = 0,
            .lcd_cmd_bits       = 8,
            .lcd_param_bits     = 8,
    };
    
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        goto lcd_init_error;
    }

    // Configure panel
    esp_lcd_dpi_panel_config_t dpi_config = ST7701_480_800_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    st7701_vendor_config_t     vendor_config = {
            .mipi_config = {
                    .dsi_bus    = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
            },
            .flags = {
                    .use_mipi_interface = 1,
            }
    };
    
    esp_lcd_panel_dev_config_t lcd_dev_config = {
            .bits_per_pixel = 16,
            .rgb_ele_order  = ESP_LCD_COLOR_SPACE_RGB,
            .reset_gpio_num = LCD_RST,
            .vendor_config  = &vendor_config,
    };
    
    ret = esp_lcd_new_panel_st7701(io, &lcd_dev_config, &disp_panel);
    if (ret != ESP_OK || disp_panel == NULL) {
        ESP_LOGE(TAG, "Failed to initialize ST7701 LCD panel: %s", 
                 ret != ESP_OK ? esp_err_to_name(ret) : "NULL handle");
        goto lcd_init_error;
    }

    // Reset panel
    ret = esp_lcd_panel_reset(disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset LCD panel: %s", esp_err_to_name(ret));
        goto lcd_init_error;
    }
    
    // Initialize panel
    ret = esp_lcd_panel_init(disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
        goto lcd_init_error;
    }
    
    // Turn on display
    ret = esp_lcd_panel_disp_on_off(disp_panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn on LCD display: %s", esp_err_to_name(ret));
        goto lcd_init_error;
    }

    ESP_LOGI(TAG, "LCD initialization completed successfully");
    return;

lcd_init_error:
    ESP_LOGE(TAG, "LCD initialization failed - application will continue without display");
    
    // Clean up resources on failure
    if (disp_panel != NULL) {
        esp_lcd_panel_del(disp_panel);
        disp_panel = NULL;
    }
    
    // Note: In a production system, you might want to:
    // - Retry initialization with different parameters
    // - Fall back to a simpler display mode
    // - Enter a safe state or restart the system
}

static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int w = (area->x2 - area->x1 + 1);
    int h = (area->y2 - area->y1 + 1);

    if (w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "Invalid flush area!");
        lv_disp_flush_ready(drv);
        return;
    }

    esp_lcd_panel_draw_bitmap(disp_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
}

void lvgl_task(void *pvParameter) {
    uint32_t tick_counter = 0;
    
    while (1) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        
        // Отладочная информация каждые 5 секунд
        if (++tick_counter % 500 == 0) { // Каждые 5 секунд при задержке 10мс
            ESP_LOGI(TAG, "LVGL task running - tick %lu", tick_counter);
            
            if (lvgl_disp != NULL) {
                ESP_LOGI(TAG, "LVGL display driver is registered");
            } else {
                ESP_LOGE(TAG, "LVGL display driver is NULL!");
            }
            
            if (lv_scr_act() != NULL) {
                ESP_LOGI(TAG, "LVGL screen is active");
            } else {
                ESP_LOGE(TAG, "LVGL screen is NULL!");
            }
        }
        
        // Периодически проверяем и скрываем курсоры мыши
        static uint32_t cursor_check_counter = 0;
        if (++cursor_check_counter % 100 == 0) { // Каждые ~1 секунду при задержке 10мс
            lv_indev_t *indev = lv_indev_get_next(NULL);
            while (indev) {
                if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && indev->cursor != NULL) {
                    if (!lv_obj_has_flag(indev->cursor, LV_OBJ_FLAG_HIDDEN)) {
                        ESP_LOGI(TAG, "Auto-hiding mouse cursor");
                        lv_obj_add_flag(indev->cursor, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                indev = lv_indev_get_next(indev);
            }
        }
        
        // Обрабатываем LVGL события
        lv_timer_handler();
        
        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(config.lvgl_task_delay_ms));
    }
}

// Универсальная функция для управления видимостью LVGL объектов
static void lv_obj_set_visibility(lv_obj_t *obj, bool visible, const char* obj_name) {
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

// Специализированные функции для обратной совместимости
void lv_label_hide(){
    if (lvgl_mutex == NULL) {
        ESP_LOGW(TAG, "Cannot hide label: LVGL mutex not initialized");
        return;
    }
    
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (label_obj != NULL) {
            lv_obj_set_visibility(label_obj, false, "label");
        } else {
            ESP_LOGW(TAG, "Cannot hide label: object is NULL");
        }
        xSemaphoreGive(lvgl_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire mutex for label hide operation");
    }
}

void lv_label_show(){
    if (lvgl_mutex == NULL) {
        ESP_LOGW(TAG, "Cannot show label: LVGL mutex not initialized");
        return;
    }
    
    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (label_obj != NULL) {
            lv_obj_set_visibility(label_obj, true, "label");
        } else {
            ESP_LOGW(TAG, "Cannot show label: object is NULL");
        }
        xSemaphoreGive(lvgl_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire mutex for label show operation");
    }
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

// QR functionality has been removed - keeping function for API compatibility
void async_draw_qr(void *data) {
    ESP_LOGW(TAG, "QR functionality is not implemented");
    
    if (data != NULL) {
        free(data);
    }
}

void async_display_text(void *data) {
    char *local_TAG = "async_display_text";
    char *text_data = (char *) data;

    ESP_LOGI(TAG,"%s: Starting text display", local_TAG);

    if (text_data != NULL) {
        display_text_lvgl(text_data);
        
        // Принудительно обновляем дисплей
        if (lvgl_disp != NULL) {
            lv_refr_now(lvgl_disp);
        }
    }

    ESP_LOGI(TAG,"%s: Finished text display", local_TAG);

    free(text_data);
}

void process_data(const char *data) {
    ESP_LOGI(TAG, "Received: %s", data);

    // Проверка максимальной длины входных данных
    if (data == NULL || strlen(data) > 1024) {
        ESP_LOGE(TAG, "Invalid data length or NULL pointer");
        return;
    }

    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        ESP_LOGE(TAG, "failed to parse string as JSON");
        return;
    }

    cJSON *type    = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsString(type) && cJSON_IsString(content)) {
        ESP_LOGI(TAG, "Type: %s, Data: %s", type->valuestring, content->valuestring);

        // Валидация длины данных перед копированием
        size_t content_len = strlen(content->valuestring);
        
        // Для команды clear разрешаем пустой контент
        if (strcmp(type->valuestring, "clear") != 0 && content_len == 0) {
            ESP_LOGE(TAG, "Invalid content length: %zu bytes", content_len);
            cJSON_Delete(root);
            return;
        }
        
        if (content_len > QR_CONTENT_MAX_LENGTH) {
            ESP_LOGE(TAG, "Content too long: %zu bytes (max %d)", content_len, QR_CONTENT_MAX_LENGTH);
            cJSON_Delete(root);
            return;
        }

        char *content_copy = strdup(content->valuestring);
        if (content_copy == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for content copy");
            cJSON_Delete(root);
            return;
        }
        
        if (strcmp(type->valuestring, "qr") == 0) {
            ESP_LOGI(TAG, "Sending a QR rendering command");
            
            #if LV_USE_QRCODE
                ESP_LOGI(TAG, "Creating QR code with data: '%s'", content_copy);
                
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

                // Создаем QR-код используя новую функцию
                bool success = create_qr_code(content_copy, qr_color, screen_bg_color);
                
                if (success) {
                    ESP_LOGI(TAG, "QR code created successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to create QR code!");
                    
                    // Показываем сообщение об ошибке
                    if (lv_scr_act() != NULL && lvgl_disp != NULL) {
                        ESP_LOGI(TAG, "Showing error message");
                        
                        bool label_success = create_label_with_text("QR Code Generation Failed", screen_bg_color);
                        if (label_success) {
                            ESP_LOGI(TAG, "Error label created successfully");
                        } else {
                            ESP_LOGE(TAG, "Failed to create error label!");
                        }
                    }
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
            #endif
            
            free(content_copy); // Освобождаем память

        } else if (strcmp(type->valuestring, "text") == 0) {
            ESP_LOGI(TAG, "Sending a text rendering command");
            
            // Очищаем экран перед отображением текста
            ESP_LOGI(TAG, "Clearing screen before text display");
            
            if (lv_scr_act() != NULL && lvgl_disp != NULL) {
                // Берем мьютекс с таймаутом
                if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    ESP_LOGI(TAG, "Mutex acquired for text command");
                    
                    lv_obj_clean(lv_scr_act());
                    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
                    
                    xSemaphoreGive(lvgl_mutex);
                    ESP_LOGI(TAG, "Mutex released after screen clear");
                } else {
                    ESP_LOGE(TAG, "Failed to acquire mutex for text command");
                }
                
                // Вызываем display_text_lvgl без мьютекса (он сам его возьмет)
                display_text_lvgl(content_copy);
                
                // Принудительно обновляем дисплей
                if (lvgl_disp) {
                    ESP_LOGI(TAG, "Triggering display refresh for text");
                    lv_refr_now(lvgl_disp);
                }
            } else {
                ESP_LOGW(TAG, "LVGL not initialized, cannot display text");
                free(content_copy);
            }
        } else if (strcmp(type->valuestring, "qr_text") == 0) {
            ESP_LOGI(TAG, "QR+Text command received");
            
            #if LV_USE_QRCODE
                ESP_LOGI(TAG, "Creating QR code with text - QR: '%s'", content_copy);
                
                // Парсим дополнительные параметры из JSON
                cJSON *text_json = cJSON_GetObjectItemCaseSensitive(root, "text");
                char *qr_text_data = NULL;
                if (cJSON_IsString(text_json) && text_json->valuestring != NULL) {
                    qr_text_data = strdup(text_json->valuestring);
                }
                
                // Парсим цвета из JSON (опциональные поля)
                lv_color_t qr_color = lv_color_black(); // По умолчанию черный
                lv_color_t screen_bg_color = lv_color_white(); // По умолчанию белый
                lv_color_t text_color = lv_palette_main(LV_PALETTE_BLUE); // По умолчанию синий
                
                cJSON *qr_color_json = cJSON_GetObjectItemCaseSensitive(root, "qr_color");
                if (cJSON_IsString(qr_color_json)) {
                    qr_color = parse_hex_color(qr_color_json->valuestring);
                }
                
                cJSON *bg_color_json = cJSON_GetObjectItemCaseSensitive(root, "bg_color");
                if (cJSON_IsString(bg_color_json)) {
                    screen_bg_color = parse_hex_color(bg_color_json->valuestring);
                }
                
                cJSON *text_color_json = cJSON_GetObjectItemCaseSensitive(root, "text_color");
                if (cJSON_IsString(text_color_json)) {
                    text_color = parse_hex_color(text_color_json->valuestring);
                }
                
                // Парсим размер шрифта (опционально)
                uint16_t font_size = 18; // По умолчанию
                cJSON *font_size_json = cJSON_GetObjectItemCaseSensitive(root, "font_size");
                if (cJSON_IsNumber(font_size_json)) {
                    uint16_t requested_size = (uint16_t)font_size_json->valuedouble;
                    
                    // Валидация диапазона размера шрифта
                    if (requested_size < 10) {
                        ESP_LOGW(TAG, "Font size %u too small, using minimum 10", requested_size);
                        font_size = 10;
                    } else if (requested_size > 32) {
                        ESP_LOGW(TAG, "Font size %u too large, using maximum 32", requested_size);
                        font_size = 32;
                    } else {
                        font_size = requested_size;
                    }
                    
                    ESP_LOGI(TAG, "Font size set to %u", font_size);
                } else {
                    ESP_LOGI(TAG, "Using default font size 18");
                }
                
                // Парсим выравнивание текста (опционально)
                lv_text_align_t text_align = LV_TEXT_ALIGN_CENTER; // По умолчанию по центру
                cJSON *text_align_json = cJSON_GetObjectItemCaseSensitive(root, "text_align");
                if (cJSON_IsString(text_align_json) && text_align_json->valuestring != NULL) {
                    const char *align_str = text_align_json->valuestring;
                    
                    if (strcmp(align_str, "left") == 0) {
                        text_align = LV_TEXT_ALIGN_LEFT;
                    } else if (strcmp(align_str, "center") == 0) {
                        text_align = LV_TEXT_ALIGN_CENTER;
                    } else if (strcmp(align_str, "right") == 0) {
                        text_align = LV_TEXT_ALIGN_RIGHT;
                    } else {
                        ESP_LOGW(TAG, "Invalid text_align '%s', using default center", align_str);
                        text_align = LV_TEXT_ALIGN_CENTER;
                    }
                    
                    ESP_LOGI(TAG, "Text align set to %s", align_str);
                } else {
                    ESP_LOGI(TAG, "Using default text align center");
                }
                
                // Создаем QR код с текстом
                bool success = create_qr_with_text_simple(content_copy, qr_text_data, qr_color, screen_bg_color, text_color, font_size, text_align);
                
                // Освобождаем память
                if (qr_text_data != NULL) {
                    free(qr_text_data);
                }
                
                if (success) {
                    ESP_LOGI(TAG, "QR code with text created successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to create QR code with text!");
                    
                    // Показываем сообщение об ошибке
                    if (lv_scr_act() != NULL && lvgl_disp != NULL) {
                        bool label_success = create_label_with_text("QR+Text Generation Failed", screen_bg_color);
                        if (label_success) {
                            ESP_LOGI(TAG, "Error label created successfully");
                        } else {
                            ESP_LOGE(TAG, "Failed to create error label!");
                        }
                    }
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
            #endif
            
            free(content_copy); // Освобождаем память
        } else if (strcmp(type->valuestring, "clear") == 0) {
            ESP_LOGI(TAG, "Clearing screen command received");
            
            // Очищаем экран синхронно
            if (lv_scr_act() != NULL && lvgl_disp != NULL) {
                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                
                ESP_LOGI(TAG, "Cleaning screen objects");
                lv_obj_clean(lv_scr_act());
                
                ESP_LOGI(TAG, "Setting white background");
                lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
                
                // Сбрасываем глобальные указатели объектов
                label_obj = NULL;
                
                #if LV_USE_QRCODE
                    qrcode_obj = NULL;
                    qr_text_qrcode_obj = NULL;
                    qr_text_label_obj = NULL;
                #endif
                
                if (lvgl_disp) {
                    ESP_LOGI(TAG, "Triggering display refresh for clear");
                    lv_refr_now(lvgl_disp);
                }
                
                xSemaphoreGive(lvgl_mutex);
            } else {
                ESP_LOGW(TAG, "LVGL not initialized, cannot clear screen");
            }
            
            free(content_copy); // Освобождаем память
        } else {
            ESP_LOGI(TAG, "Unknown command type: %s", type->valuestring);
            free(content_copy); // Освобождаем, если не использовали
        }
    } else {
        ESP_LOGI(TAG, "Invalid JSON structure: missing 'type' or 'data' fields");
        ESP_LOGI(TAG, "Type is string: %s", cJSON_IsString(type) ? "yes" : "no");
        ESP_LOGI(TAG, "Content is string: %s", cJSON_IsString(content) ? "yes" : "no");
    }
    cJSON_Delete(root);
}

void usb_rx_task(void *arg) {
    char buffer[USB_RX_BUFFER_SIZE];
    int index = 0;
    
    // Initialize buffer to prevent garbage data
    memset(buffer, 0, sizeof(buffer));
    
    while (1) {
        int c = fgetc(stdin);
        if (c != EOF) {
            if (c == '\n' || c == '\r') {
                if (index > 0) {
                    buffer[index] = '\0';
                    
                    // Validate data length before processing
                    if (index < USB_RX_BUFFER_SIZE - 1) {
                        process_data(buffer);
                    } else {
                        ESP_LOGW(TAG, "USB data too long (%d bytes), discarding", index);
                    }
                }
                // Reset buffer and index
                memset(buffer, 0, sizeof(buffer));
                index = 0;
            } else {
                if (index < sizeof(buffer) - 2) { // Reserve space for null terminator
                    buffer[index++] = (char) c;
                } else {
                    // Buffer overflow protection - clear and reset
                    ESP_LOGW(TAG, "USB buffer overflow detected (max %d bytes), resetting", USB_RX_BUFFER_SIZE);
                    memset(buffer, 0, sizeof(buffer));
                    index = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {

    ESP_LOGI(TAG, "=== Starting ESP32 Display Application ===");
    
    // Инициализируем дисплей
    init_lcd();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Включаем подсветку
    init_backlight();
    
    ESP_LOGI(TAG, "=== Initializing LVGL ===");
    
    lv_init();

    // Оптимизированный размер буфера
    uint32_t screen_pixels = config.lcd_h_res * config.lcd_v_res;
    uint32_t buf_size = screen_pixels / LVGL_BUFFER_FACTOR; // 1/4 экрана
    
    ESP_LOGI(TAG, "Using LVGL buffer size: %lu pixels (%.1f KB)", 
             buf_size, (buf_size * sizeof(lv_color_t)) / 1024.0);
    
    buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        
        // Proper cleanup for partial allocations
        if (buf1 != NULL) {
            heap_caps_free(buf1);
            buf1 = NULL;
        }
        if (buf2 != NULL) {
            heap_caps_free(buf2);
            buf2 = NULL;
        }
        
        return;
    }

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_size);

    lv_disp_drv_t     disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = config.lcd_h_res;
    disp_drv.ver_res  = config.lcd_v_res;
    disp_drv.flush_cb = lv_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    
    // Отключаем все ненужные функции драйвера
    disp_drv.sw_rotate = 0;
    disp_drv.rotated = LV_DISP_ROT_NONE;
    
    lvgl_disp = lv_disp_drv_register(&disp_drv);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to register LVGL display driver");
        
        // Cleanup allocated buffers
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        buf1 = buf2 = NULL;
        
        return;
    }

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
            .on_color_trans_done = test_notify_refresh_ready,
    };
    
    if (disp_panel != NULL) {
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, &disp_drv));
    }

    // Очищаем экран и устанавливаем белый фон
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
    
    // Initialize label style once
    lv_style_init(&label_style);
    lv_style_set_text_align(&label_style, LV_TEXT_ALIGN_CENTER);
    
    ESP_LOGI(TAG, "LVGL initialized successfully");

    // Application initialized successfully - ready for JSON commands via USB

    /*
     Поддерживаемые JSON команды:
     
     // QR коды (максимальный размер 80% экрана, ECC L)
     // Базовый QR код (черный на белом)
     {"type": "qr", "data": "https://example.com"}
     
     // QR код с пользовательскими цветами
     {"type": "qr", "data": "https://example.com", "qr_color": "#FF0000", "bg_color": "#00FF00"}
     {"type": "qr", "data": "123", "qr_color": "#0000FF"}
     {"type": "qr", "data": "test", "bg_color": "#FFFF00"}
     
     // QR код с текстом (новая функция!)
     {"type": "qr_text", "data": "https://example.com", "text": "Visit our website!"}
     {"type": "qr_text", "data": "product-12345", "text": "Product Info", "qr_color": "#FF0000", "bg_color": "#FFFF00", "text_color": "#0000FF"}
     
     // Текстовые команды
     {"type": "text", "data": "Hello World"}
     
     // Команда очистки экрана
     {"type": "clear", "data": ""}
     
     // Примеры цветов:
     // #FF0000 - красный
     // #00FF00 - зеленый  
     // #0000FF - синий
     // #FFFF00 - желтый
     // #FFFFFF - белый
     // #000000 - черный
     
     char *qr_data = "236299fc-ebb7-42f0-8064-a68d794df943";
     https://www.meme-arsenal.com/memes/648da849201ad7d325466cc03afb2703.jpg
     
     // QR+Text Layout:
     // - QR код: 70% ширины экрана, позиция 20% от верха
     // - Текст: под QR кодом с отступом 30px, центрирование
     // - Максимальная ширина текста: экран - 40px (отступы по бокам)
    */

    // Create LVGL mutex
    lvgl_mutex = xSemaphoreCreateMutex();
    if (lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        
        // Cleanup allocated resources
        if (buf1 != NULL) {
            heap_caps_free(buf1);
            buf1 = NULL;
        }
        if (buf2 != NULL) {
            heap_caps_free(buf2);
            buf2 = NULL;
        }
        
        return;
    }

    TaskHandle_t lvgl_task_handle, usb_rx_task_handle;
    
    BaseType_t ret = xTaskCreate(lvgl_task,  "lvgl_task", 4096, NULL, 5, &lvgl_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        
        // Cleanup resources
        vSemaphoreDelete(lvgl_mutex);
        if (buf1 != NULL) {
            heap_caps_free(buf1);
            buf1 = NULL;
        }
        if (buf2 != NULL) {
            heap_caps_free(buf2);
            buf2 = NULL;
        }
        
        return;
    }
    
    ret = xTaskCreate(usb_rx_task, "usb_rx_task", 4096, NULL, 5, &usb_rx_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB RX task");
        
        // Cleanup resources
        vTaskDelete(lvgl_task_handle);
        vSemaphoreDelete(lvgl_mutex);
        if (buf1 != NULL) {
            heap_caps_free(buf1);
            buf1 = NULL;
        }
        if (buf2 != NULL) {
            heap_caps_free(buf2);
            buf2 = NULL;
        }
        
        return;
    }
    
    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "Application ready - waiting for JSON commands via USB");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
