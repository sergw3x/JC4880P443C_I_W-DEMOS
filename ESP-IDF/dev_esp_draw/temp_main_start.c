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
#include "command_processor.h"
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
#define USB_RX_BUFFER_SIZE                         2048         // USB input buffer size in bytes

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
