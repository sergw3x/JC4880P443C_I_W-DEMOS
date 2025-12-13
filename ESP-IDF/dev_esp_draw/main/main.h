//
// Created by Сергей Шкляр on 13.12.2025.
//

#ifndef DEV_ESP_DRAW_MAIN_H
#define DEV_ESP_DRAW_MAIN_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
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

//#include "app_config.h"
//#include "display.h"
#include "utils.h"
#include "logo.h"
#include "images/checkin_ok.c"
#include "images/checkin_false.c"

// LCD Hardware Configuration
#define LCD_BACKLIGHT                              (GPIO_NUM_23)
#define LCD_RST                                    (GPIO_NUM_5)

// Разрешение экрана
#define LCD_H_RES                                  (480)       // Horizontal resolution in pixels
#define LCD_V_RES                                  (800)       // Vertical resolution in pixels


// Макросы для замера производительности
#define PERF_TAG "PERF"
#define LOG_EXECUTION_TIME(func_name, time_us) \
    ESP_LOGI(PERF_TAG, "%s: %lld us (%d ms)", func_name, time_us, (int)(time_us/1000))

#define MEASURE_FUNCTION_START() \
    int64_t perf_start_ = esp_timer_get_time()

#define MEASURE_FUNCTION_END(func_name) \
    int64_t perf_end_ = esp_timer_get_time(); \
    LOG_EXECUTION_TIME(func_name, perf_end_ - perf_start_)

// Конфигурационная структура для QR кода
typedef struct {
    const char *qr_data;
    const char *text_data;
    lv_color_t qr_color;
    lv_color_t bg_color;
    lv_color_t text_color;
    uint16_t font_size;
    lv_text_align_t text_align;
} qr_config_t;

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

// MIPI DSI Configuration
#define BSP_LCD_MIPI_DSI_LANE_NUM                  (2)         // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS         (1500)      // 1Gbps
#define LANE_BITRATE_MBPS                          1000         // Lane bit rate in Mbps

// Power Management
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN              (3)         // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV        (2500)      // LDO voltage in millivolts

// LVGL Configuration
#define V_TASK_DELAY                               (16)          // LVGL task delay in milliseconds
#define LVGL_BUFFER_SIZE                           (LCD_H_RES * LCD_V_RES)
#define LVGL_BUFFER_FACTOR                         (1)           // Buffer size = screen_size / factor

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

// QR Code Configuration
#define QR_CONTENT_MAX_LENGTH                      (512)       // Maximum QR content length in bytes

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

// Макрос для безопасной работы с мьютексом LVGL
#define WITH_LVGL_MUTEX(timeout_ms, code_block) do { \
    if (lvgl_mutex == NULL) { \
        ESP_LOGW(TAG, "LVGL mutex not initialized"); \
    } else if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) { \
        do { code_block } while(0); \
        xSemaphoreGive(lvgl_mutex); \
    } else { \
        ESP_LOGW(TAG, "Failed to acquire LVGL mutex within %d ms", timeout_ms); \
    } \
} while(0)

static void safe_label_delete(void);
static void safe_qrcode_delete(void);

static bool create_label_with_text(const char *text, lv_color_t bg_color);

static void cleanup_qr_text_objects(void);
static bool create_qr_unified(const qr_config_t *config);

static void init_lcd(void);
static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);

// Унифицированная функция управления видимостью объектов
static void set_object_visibility(lv_obj_t *obj, bool visible, const char *obj_name);

void lv_label_hide(void);
void lv_label_show(void);
void display_text_lvgl(const char *text);
void async_display_text(void *data);
void process_data(const char *data);
void usb_rx_task(void *arg);

// Централизованная функция очистки ресурсов
static void cleanup_resources(void);

// Улучшенная функция валидации параметров
static bool validate_init_params(app_config_t *cfg);

#endif //DEV_ESP_DRAW_MAIN_H
