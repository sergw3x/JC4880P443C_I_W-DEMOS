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

#include "images/hex_logo.c"

#if LV_USE_QRCODE
#include "extra/libs/qrcode/lv_qrcode.h"
#endif

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

// Глобальные переменные LVGL
static lv_obj_t *label_obj = NULL;
static lv_style_t label_style;
#if LV_USE_QRCODE
static lv_obj_t *qrcode_obj = NULL;
#endif
static SemaphoreHandle_t lvgl_mutex = NULL;

// Глобальные переменные дисплея
static const char *TAG = "esp_draw_bit";
const uint16_t white_color = 0xFFFF;
const uint16_t black_color = 0x0000;
static esp_lcd_panel_handle_t disp_panel = NULL;
static lv_disp_draw_buf_t disp_buf;
static lv_disp_t *lvgl_disp = NULL;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

extern const lv_font_t font_roboto_24_cyr;
extern const lv_font_t font_roboto_28_cyr;
extern const lv_font_t font_roboto_32_cyr;
extern const lv_font_t font_roboto_36_cyr;
extern const lv_font_t font_roboto_40_cyr;
extern const lv_font_t font_roboto_44_cyr;
extern const lv_font_t font_roboto_48_cyr;
extern const lv_font_t font_roboto_52_cyr;
extern const lv_font_t font_roboto_56_cyr;
extern const lv_font_t font_roboto_60_cyr;
extern const lv_font_t font_roboto_64_cyr;
extern const lv_font_t font_roboto_68_cyr;

// Безопасная функция для удаления label объекта
static void safe_label_delete(void) {
    if (label_obj != NULL && lv_obj_is_valid(label_obj)) {
        ESP_LOGI(TAG, "safe_label_delete: Deleting label object at %p", label_obj);
        lv_obj_del(label_obj);
        label_obj = NULL;
    }
}

#if LV_USE_QRCODE
// Безопасная функция для удаления QR объекта
static void safe_qrcode_delete(void) {
    if (qrcode_obj != NULL && lv_obj_is_valid(qrcode_obj)) {
        ESP_LOGI(TAG, "safe_qrcode_delete: Deleting QR object at %p", qrcode_obj);
        lv_obj_del(qrcode_obj);
        qrcode_obj = NULL;
    }
}
#endif

// Универсальная функция для создания и настройки label объекта
static bool create_label_with_text(const char *text, lv_color_t bg_color) {
    MEASURE_FUNCTION_START();

    if (lv_scr_act() == NULL || lvgl_disp == NULL) {
        ESP_LOGE(TAG, "create_label_with_text: LVGL not initialized");
        MEASURE_FUNCTION_END("LVGL_not_init");
        return false;
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_label_with_text: Failed to acquire mutex");
        MEASURE_FUNCTION_END("Mutex_acquire_fail");
        return false;
    }

    ESP_LOGI(TAG, "create_label_with_text: Starting with text='%s'", text);

    // Удаляем существующие объекты
    safe_label_delete();
#if LV_USE_QRCODE
    safe_qrcode_delete();
#endif

    // Очищаем экран и устанавливаем фон
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), bg_color, 0);

    // Создаем новый объект
    label_obj = lv_label_create(lv_scr_act());

    if (label_obj == NULL) {
        ESP_LOGE(TAG, "create_label_with_text: Failed to create label object!");
        xSemaphoreGive(lvgl_mutex);
        MEASURE_FUNCTION_END("Label_create_failed");
        return false;
    }

    // Настраиваем объект
    lv_obj_set_width(label_obj, LV_PCT(90));
    lv_obj_center(label_obj);

    // Устанавливаем стили
    lv_obj_add_style(label_obj, &label_style, 0);

    // Устанавливаем цвет текста
    lv_color_t text_color = lv_palette_main(LV_PALETTE_BLUE);
    lv_obj_set_style_text_color(label_obj, text_color, 0);

    if (text == NULL) {
        text = "";
    }

    lv_label_set_text(label_obj, text);
    lv_obj_set_style_text_font(label_obj, &font_roboto_24_cyr, LV_PART_MAIN);

    // Принудительно обновляем дисплей
    lv_refr_now(lvgl_disp);

    xSemaphoreGive(lvgl_mutex);
    ESP_LOGI(TAG, "create_label_with_text: Completed successfully");

    MEASURE_FUNCTION_END("Create_Label_Text");
    return true;
}

// Глобальные переменные для QR+Text
#if LV_USE_QRCODE
static lv_obj_t *qr_text_qrcode_obj = NULL;
static lv_obj_t *qr_text_label_obj = NULL;
#endif

/**
 * @brief Выбирает шрифт LVGL по размеру
 */
static const lv_font_t *get_font_by_size_simple(uint16_t font_size) {
    switch (font_size) {
        case 24:
            return &font_roboto_24_cyr;
        case 28:
            return &font_roboto_28_cyr;
        case 32:
            return &font_roboto_32_cyr;
        case 36:
            return &font_roboto_36_cyr;
        case 40:
            return &font_roboto_40_cyr;
        case 44:
            return &font_roboto_44_cyr;
        case 48:
            return &font_roboto_48_cyr;
        case 52:
            return &font_roboto_52_cyr;
        case 56:
            return &font_roboto_56_cyr;
        case 60:
            return &font_roboto_60_cyr;
        case 64:
            return &font_roboto_64_cyr;
        case 68:
            return &font_roboto_68_cyr;
        default:
            if (font_size < 24) return &font_roboto_24_cyr;
            if (font_size > 68) return &font_roboto_68_cyr;
            if (font_size <= 32) return &font_roboto_32_cyr;
            if (font_size <= 40) return &font_roboto_40_cyr;
            if (font_size <= 48) return &font_roboto_48_cyr;
            if (font_size <= 56) return &font_roboto_56_cyr;
            if (font_size <= 64) return &font_roboto_64_cyr;
            return &font_roboto_44_cyr;
    }
}

// Forward declaration for text formatting function
static char *process_text_formatting_simple(const char *text);

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

// Универсальная функция для очистки QR+Text объектов
static void cleanup_qr_text_objects(void) {
    if (qr_text_qrcode_obj != NULL && lv_obj_is_valid(qr_text_qrcode_obj)) {
        lv_obj_del(qr_text_qrcode_obj);
        qr_text_qrcode_obj = NULL;
    }

    if (qr_text_label_obj != NULL && lv_obj_is_valid(qr_text_label_obj)) {
        lv_obj_del(qr_text_label_obj);
        qr_text_label_obj = NULL;
    }
}

// Унифицированная функция для создания QR кода с опциональным текстом
static bool create_qr_unified(const qr_config_t *config) {
    MEASURE_FUNCTION_START();

    if (lv_scr_act() == NULL || lvgl_disp == NULL) {
        ESP_LOGE(TAG, "create_qr_unified: LVGL not initialized");
        MEASURE_FUNCTION_END("LVGL_not_init");
        return false;
    }

    if (config == NULL || config->qr_data == NULL) {
        ESP_LOGE(TAG, "create_qr_unified: Invalid config or QR data");
        MEASURE_FUNCTION_END("Invalid_config");
        return false;
    }

    if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "create_qr_unified: Failed to acquire mutex");
        MEASURE_FUNCTION_END("Mutex_acquire_fail");
        return false;
    }

    ESP_LOGI(TAG, "create_qr_unified: QR data: '%s', Text: '%s'",
             config->qr_data, config->text_data ? config->text_data : "NULL");

    // Очищаем существующие объекты
    cleanup_qr_text_objects();

    // Также очищаем другие QR объекты для консистентности
    safe_qrcode_delete();
    safe_label_delete();

    // Очищаем экран и устанавливаем фон
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), config->bg_color, 0);

    // Получаем размеры экрана
    uint16_t screen_width = LCD_H_RES;
    uint16_t screen_height = LCD_V_RES;

    // Вычисляем размеры и позиции QR кода
    uint16_t qr_size;
    uint16_t qr_y;

    if (config->text_data != NULL && strlen(config->text_data) > 0) {
        // Режим QR + текст
        qr_size = (uint16_t)(screen_width * 0.7);  // 70% ширины экрана
        qr_y = screen_height * 0.2;                // QR код на 20% от верха
    } else {
        // Режим только QR - используем максимальный размер 80%
        qr_size = (uint16_t)(MIN(screen_width, screen_height) * 0.8);
        if (qr_size < 100) qr_size = MIN(screen_width, screen_height);
        qr_y = (screen_height - qr_size) / 2;       // Центрируем по вертикали
    }

    // Ограничиваем максимальный размер
    if (qr_size > screen_width * 0.8) qr_size = (uint16_t)(screen_width * 0.8);
    if (qr_y < 10) qr_y = 10;

#if LV_USE_QRCODE
    // Создаем QR код
    qr_text_qrcode_obj = lv_qrcode_create(lv_scr_act(), qr_size, config->qr_color, config->bg_color);

    if (qr_text_qrcode_obj == NULL) {
        ESP_LOGE(TAG, "create_qr_unified: Failed to create QR object!");
        xSemaphoreGive(lvgl_mutex);
        MEASURE_FUNCTION_END("QR_create_failed");
        return false;
    }

    // Позиционируем QR код
    if (config->text_data != NULL && strlen(config->text_data) > 0) {
        lv_obj_set_pos(qr_text_qrcode_obj, (screen_width - qr_size) / 2, qr_y);
    } else {
        lv_obj_center(qr_text_qrcode_obj);
    }

    // Устанавливаем данные QR-кода
    lv_res_t result = lv_qrcode_update(qr_text_qrcode_obj, config->qr_data, strlen(config->qr_data));
    
    if (result != LV_RES_OK) {
        ESP_LOGE(TAG, "create_qr_unified: Failed to update QR data!");
        lv_obj_del(qr_text_qrcode_obj);
        qr_text_qrcode_obj = NULL;
        xSemaphoreGive(lvgl_mutex);
        MEASURE_FUNCTION_END("QR_update_failed");
        return false;
    }
#endif

    // Создаем текстовый объект если нужно
    bool text_created = false;
    if (config->text_data != NULL && strlen(config->text_data) > 0) {
        qr_text_label_obj = lv_label_create(lv_scr_act());

        if (qr_text_label_obj == NULL) {
            ESP_LOGE(TAG, "create_qr_unified: Failed to create label object!");
#if LV_USE_QRCODE
            lv_obj_del(qr_text_qrcode_obj);
            qr_text_qrcode_obj = NULL;
#endif
            xSemaphoreGive(lvgl_mutex);
            MEASURE_FUNCTION_END("Text_label_create_failed");
            return false;
        }

        // Настраиваем текстовый объект
        lv_obj_set_width(qr_text_label_obj, screen_width - 40); // Отступы по бокам

        uint16_t text_y = qr_y + qr_size + 30; // Текст под QR кодом с отступом
        lv_obj_set_pos(qr_text_label_obj, 20, text_y);

        // Настраиваем текстовый объект напрямую без временного стиля
        const lv_font_t *selected_font = get_font_by_size_simple(config->font_size);

        ESP_LOGI(TAG, "create_qr_unified: Using font size %u (font: %p)", config->font_size, selected_font);

        // Применяем стили напрямую к объекту
        lv_obj_set_style_text_font(qr_text_label_obj, selected_font, 0);
        lv_obj_set_style_text_align(qr_text_label_obj, config->text_align, 0);
        lv_obj_set_style_text_color(qr_text_label_obj, config->text_color, 0);

        // Обрабатываем и устанавливаем текст
        char *processed_text = process_text_formatting_simple(config->text_data);
        lv_label_set_text(qr_text_label_obj, processed_text ? processed_text : config->text_data);

        // Освобождаем память обработанного текста
        if (processed_text != NULL) {
            free(processed_text);
        }

        text_created = true;
    }

    // Принудительно обновляем дисплей
    lv_refr_now(lvgl_disp);

    xSemaphoreGive(lvgl_mutex);

    const char *mode = text_created ? "QR+Text" : "QR only";
    ESP_LOGI(TAG, "create_qr_unified: Completed successfully (%s)", mode);

    MEASURE_FUNCTION_END("Create_QR_Unified");
    return true;
}

// Удалена дублирующаяся функция create_qr_code - заменена на create_qr_unified

// Функция для парсинга hex цвета в формате "#RRGGBB" или "RRGGBB"
static lv_color_t parse_hex_color(const char *color_str) {
    MEASURE_FUNCTION_START();

    if (color_str == NULL || strlen(color_str) < 6) {
        ESP_LOGW("parse_hex_color", "Invalid color string: %s", color_str ? color_str : "NULL");
        MEASURE_FUNCTION_END("Parse_hex_color_invalid");
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
        MEASURE_FUNCTION_END("Parse_hex_color_invalid_format");
        return lv_color_black();
    }

    // Парсим RGB компоненты
    char r_str[3] = {hex_start[0], hex_start[1], '\0'};
    char g_str[3] = {hex_start[2], hex_start[3], '\0'};
    char b_str[3] = {hex_start[4], hex_start[5], '\0'};

    uint8_t
            r = (uint8_t)
    strtol(r_str, NULL, 16);
    uint8_t
            g = (uint8_t)
    strtol(g_str, NULL, 16);
    uint8_t
            b = (uint8_t)
    strtol(b_str, NULL, 16);

    ESP_LOGI("parse_hex_color", "Parsed color %s -> RGB(%d,%d,%d)", color_str, r, g, b);

    MEASURE_FUNCTION_END("Parse_hex_color");
    return lv_color_make(r, g, b);
}

/**
 * @brief Обрабатывает текст, заменяя маркеры {newline} на реальные переносы строк
 */
static char *process_text_formatting_simple(const char *text) {
    MEASURE_FUNCTION_START();

    if (text == NULL) {
        MEASURE_FUNCTION_END("Process_text_formatting_null");
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
        MEASURE_FUNCTION_END("Process_text_formatting_no_markers");
        return strdup(text);
    }

    // Вычисляем новую длину строки
    size_t original_len = strlen(text);
    size_t new_len = original_len - (newline_count * 9) + newline_count; // Заменяем "{newline}" на "\n"

    // Выделяем память для новой строки
    char *processed_text = malloc(new_len + 1);
    if (processed_text == NULL) {
        ESP_LOGE(TAG, "process_text_formatting_simple: Failed to allocate memory for processed text");
        MEASURE_FUNCTION_END("Process_text_allocation_failed");
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

    MEASURE_FUNCTION_END("Process_text_formatting_simple");
    return processed_text;
}

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

IRAM_ATTR static bool
test_notify_refresh_ready(esp_lcd_panel_handle_t
panel,
esp_lcd_dpi_panel_event_data_t *edata,
void *user_ctx
) {
lv_disp_drv_t *drv = (lv_disp_drv_t *) user_ctx;
BaseType_t need_yield = pdFALSE;

ESP_EARLY_LOGI(TAG,
"DMA done, calling lv_disp_flush_ready");

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
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_config = {
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
    st7701_vendor_config_t vendor_config = {
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
}

static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    MEASURE_FUNCTION_START();

    int w = (area->x2 - area->x1 + 1);
    int h = (area->y2 - area->y1 + 1);

    if (w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "Invalid flush area!");
        MEASURE_FUNCTION_END("LV_flush_invalid_area");
        lv_disp_flush_ready(drv);
        return;
    }

    esp_lcd_panel_draw_bitmap(disp_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);

    MEASURE_FUNCTION_END("LV_flush_cb");
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

        // Обрабатываем LVGL события
        lv_timer_handler();

        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(config.lvgl_task_delay_ms));
    }
}

// Удалена неиспользуемая функция lv_obj_set_visibility - функционал интегрирован в другие функции

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

// Унифицированная функция управления видимостью объектов
static void set_object_visibility(lv_obj_t *obj, bool visible, const char *obj_name) {
    if (obj == NULL || obj_name == NULL) {
        ESP_LOGW(TAG, "Cannot set visibility: invalid parameters");
        return;
    }

    if (visible && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return; // Уже видимый
    }

    if (!visible && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return; // Уже скрытый
    }

    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "%s shown", obj_name);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "%s hidden", obj_name);
    }
}

// Оптимизированные функции для обратной совместимости
void lv_label_hide(void) {
    MEASURE_FUNCTION_START();

    WITH_LVGL_MUTEX(100, {
        if (label_obj != NULL) {
            set_object_visibility(label_obj, false, "label");
        } else {
            ESP_LOGW(TAG, "Cannot hide label: object is NULL");
        }
    });

    MEASURE_FUNCTION_END("LV_label_hide");
}

void lv_label_show(void) {
    MEASURE_FUNCTION_START();

    WITH_LVGL_MUTEX(100, {
        if (label_obj != NULL) {
            set_object_visibility(label_obj, true, "label");
        } else {
            ESP_LOGW(TAG, "Cannot show label: object is NULL");
        }
    });

    MEASURE_FUNCTION_END("LV_label_show");
}

void display_text_lvgl(const char *text) {
    MEASURE_FUNCTION_START();

    if (text == NULL) {
        ESP_LOGE(TAG, "display_text: Text is NULL");
        MEASURE_FUNCTION_END("Display_text_null");
        return;
    }

    ESP_LOGI(TAG, "display_text: Called with text: '%s'", text);

    // Используем универсальную функцию для создания label
    bool success = create_label_with_text(text, lv_color_white());

    if (success) {
        ESP_LOGI(TAG, "display_text: Text display completed successfully");
    } else {
        ESP_LOGE(TAG, "display_text: Failed to display text");
    }

    MEASURE_FUNCTION_END("Display_text_LVGL");
}

// Удалена неиспользуемая функция async_draw_qr - QR функционал реализован через create_qr_unified

void async_display_text(void *data) {
    MEASURE_FUNCTION_START();

    char *text_data = (char *) data;

    ESP_LOGI(TAG, "async_display_text: Starting text display");

    if (text_data != NULL) {
        display_text_lvgl(text_data);

        // Принудительно обновляем дисплей
        if (lvgl_disp != NULL) {
            lv_refr_now(lvgl_disp);
        }
    }

    ESP_LOGI(TAG, "async_display_text: Finished text display");

    free(text_data);

    MEASURE_FUNCTION_END("Async_display_text");
}

void process_data(const char *data) {
    MEASURE_FUNCTION_START();

    ESP_LOGI(TAG, "Received: %s", data);

    // Проверка максимальной длины входных данных
    if (data == NULL || strlen(data) > 1024) {
        ESP_LOGE(TAG, "Invalid data length or NULL pointer");
        MEASURE_FUNCTION_END("Process_data_invalid");
        return;
    }

    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        ESP_LOGE(TAG, "failed to parse string as JSON");
        MEASURE_FUNCTION_END("Process_data_parse_failed");
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsString(type) && cJSON_IsString(content)) {
        ESP_LOGI(TAG, "Type: %s, Data: %s", type->valuestring, content->valuestring);

        // Валидация длины данных перед копированием
        size_t content_len = strlen(content->valuestring);

        // Для команды clear разрешаем пустой контент
        if (strcmp(type->valuestring, "clear") != 0 && content_len == 0) {
            ESP_LOGE(TAG, "Invalid content length: %zu bytes", content_len);
            cJSON_Delete(root);
            MEASURE_FUNCTION_END("Process_data_invalid_length");
            return;
        }

        if (content_len > QR_CONTENT_MAX_LENGTH) {
            ESP_LOGE(TAG, "Content too long: %zu bytes (max %d)", content_len, QR_CONTENT_MAX_LENGTH);
            cJSON_Delete(root);
            MEASURE_FUNCTION_END("Process_data_content_too_long");
            return;
        }

        char *content_copy = strdup(content->valuestring);
        if (content_copy == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for content copy");
            cJSON_Delete(root);
            MEASURE_FUNCTION_END("Process_data_alloc_failed");
            return;
        }

        if (strcmp(type->valuestring, "qr") == 0) {
            ESP_LOGI(TAG, "Sending a QR rendering command");


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

            // Создаем QR-код используя унифицированную функцию
            qr_config_t qr_config = {
                .qr_data = content_copy,
                .text_data = NULL,  // Только QR код
                .qr_color = qr_color,
                .bg_color = screen_bg_color,
                .text_color = lv_color_black(),  // Не используется для QR only
                .font_size = 24,  // Не используется для QR only
                .text_align = LV_TEXT_ALIGN_CENTER  // Не используется для QR only
            };

            bool success = create_qr_unified(&qr_config);

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
                uint16_t
                        requested_size = (uint16_t)
                font_size_json->valuedouble;

                // Валидация диапазона размера шрифта
                if (requested_size < 24) {
                    ESP_LOGW(TAG, "Font size %u too small, using minimum 24", requested_size);
                    font_size = 24;
                } else if (requested_size > 68) {
                    ESP_LOGW(TAG, "Font size %u too large, using maximum 68", requested_size);
                    font_size = 68;
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

            // Создаем QR код с текстом используя унифицированную функцию
            qr_config_t qr_text_config = {
                .qr_data = content_copy,
                .text_data = qr_text_data,
                .qr_color = qr_color,
                .bg_color = screen_bg_color,
                .text_color = text_color,
                .font_size = font_size,
                .text_align = text_align
            };

            bool success = create_qr_unified(&qr_text_config);

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


                qrcode_obj = NULL;
                qr_text_qrcode_obj = NULL;
                qr_text_label_obj = NULL;


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

    MEASURE_FUNCTION_END("Process_data");
    cJSON_Delete(root);
}

void usb_rx_task(void *arg) {
    char buffer[USB_RX_BUFFER_SIZE];
    int index = 0;

    // Initialize buffer to prevent garbage data
    memset(buffer, 0, sizeof(buffer));

    ESP_LOGI(TAG, "USB RX task started - optimized with dynamic delay");

    while (1) {
        int c = fgetc(stdin);
        if (c != EOF) {
            // Data available - process immediately without delay
            if (c == '\n' || c == '\r') {
                if (index > 0) {
                    buffer[index] = '\0';
                    // Validate data length before processing
                    if (index < USB_RX_BUFFER_SIZE - 1) {
                        ESP_LOGI(TAG, "Processing complete command (%d bytes)", index);
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
        } else {
            // No data available - short delay to save CPU
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

// Централизованная функция очистки ресурсов
static void cleanup_resources(void) {
    ESP_LOGI(TAG, "Cleaning up application resources");

    // Очищаем LVGL объекты
    safe_label_delete();
#if LV_USE_QRCODE
    safe_qrcode_delete();
    cleanup_qr_text_objects();
#endif

    // Очищаем стиль
    lv_style_reset(&label_style);

    // Освобождаем буферы
    if (buf1 != NULL) {
        heap_caps_free(buf1);
        buf1 = NULL;
    }

    if (buf2 != NULL) {
        heap_caps_free(buf2);
        buf2 = NULL;
    }

    // Удаляем мьютекс
    if (lvgl_mutex != NULL) {
        vSemaphoreDelete(lvgl_mutex);
        lvgl_mutex = NULL;
    }

    // Очищаем дисплей панель
    if (disp_panel != NULL) {
        esp_lcd_panel_del(disp_panel);
        disp_panel = NULL;
    }
}

// Улучшенная функция валидации параметров
static bool validate_init_params(void) {
    if (config.lcd_h_res == 0 || config.lcd_v_res == 0) {
        ESP_LOGE(TAG, "Invalid LCD resolution: %dx%d", config.lcd_h_res, config.lcd_v_res);
        return false;
    }

    if (config.lvgl_buffer_factor == 0) {
        ESP_LOGE(TAG, "Invalid LVGL buffer factor: %lu", config.lvgl_buffer_factor);
        return false;
    }

    if (config.backlight_level > 100) {
        ESP_LOGW(TAG, "Clamping backlight level from %u to 100", config.backlight_level);
        ((app_config_t*)&config)->backlight_level = 100;
    }

    return true;
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Starting ESP32 Display Application ===");

    // Валидация параметров конфигурации
    if (!validate_init_params()) {
        ESP_LOGE(TAG, "Configuration validation failed");
        return;
    }

    // Инициализируем дисплей
    init_lcd();

    // Проверка успешности инициализации дисплея
    if (disp_panel == NULL) {
        ESP_LOGW(TAG, "LCD initialization failed, continuing with limited functionality");
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Включаем подсветку
    init_backlight();

    ESP_LOGI(TAG, "=== Initializing LVGL ===");

    // Инициализация LVGL
    lv_init();

    // Оптимизированный размер буфера с проверкой переполнения
    uint64_t screen_pixels = (uint64_t)config.lcd_h_res * config.lcd_v_res;
    if (screen_pixels > UINT32_MAX / sizeof(lv_color_t)) {
        ESP_LOGE(TAG, "Screen size too large for buffer calculation");
        cleanup_resources();
        return;
    }

    uint32_t buf_size = (uint32_t)(screen_pixels / config.lvgl_buffer_factor);

    ESP_LOGI(TAG, "Using LVGL buffer size: %lu pixels (%.1f KB)",
             buf_size, (buf_size * sizeof(lv_color_t)) / 1024.0);

    // Выделяем буферы с улучшенной обработкой ошибок
    buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf1 == NULL) {
        ESP_LOGW(TAG, "Failed to allocate SPIRAM buffer for buf1, trying regular RAM");
        buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DEFAULT);
    }

    buf2 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf2 == NULL) {
        ESP_LOGW(TAG, "Failed to allocate SPIRAM buffer for buf2, trying regular RAM");
        buf2 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DEFAULT);
    }

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        cleanup_resources();
        return;
    }

    // Инициализация буфера отображения
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_size);

    // Настройка драйвера дисплея
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = config.lcd_h_res;
    disp_drv.ver_res = config.lcd_v_res;
    disp_drv.flush_cb = lv_flush_cb;
    disp_drv.draw_buf = &disp_buf;

    // Отключаем ненужные функции для оптимизации
    disp_drv.sw_rotate = 0;
    disp_drv.rotated = LV_DISP_ROT_NONE;

    // Регистрация драйвера
    lvgl_disp = lv_disp_drv_register(&disp_drv);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to register LVGL display driver");
        cleanup_resources();
        return;
    }

    // Настройка callbacks для DMA
    if (disp_panel != NULL) {
        esp_lcd_dpi_panel_event_callbacks_t cbs = {
                .on_color_trans_done = test_notify_refresh_ready,
        };

        esp_err_t ret = esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, &disp_drv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register DPI callbacks: %s", esp_err_to_name(ret));
        }
    }

    // Очищаем экран и устанавливаем белый фон
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);

    lv_obj_t * img = lv_img_create(lv_scr_act());
    lv_img_set_src(img, &hex_logo);
    lv_obj_center(img);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    // Инициализация стилей
    lv_style_init(&label_style);
    lv_style_set_text_align(&label_style, LV_TEXT_ALIGN_CENTER);

    ESP_LOGI(TAG, "LVGL initialized successfully");

    // Создаем мьютекс
    lvgl_mutex = xSemaphoreCreateMutex();
    if (lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        cleanup_resources();
        return;
    }

    // Создаем задачи с улучшенной обработкой ошибок
    TaskHandle_t lvgl_task_handle = NULL;
    TaskHandle_t usb_rx_task_handle = NULL;

    BaseType_t ret = xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, &lvgl_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        cleanup_resources();
        return;
    }

    ret = xTaskCreate(usb_rx_task, "usb_rx_task", 4096, NULL, 5, &usb_rx_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB RX task");

        // Очищаем созданную задачу LVGL
        if (lvgl_task_handle != NULL) {
            vTaskDelete(lvgl_task_handle);
        }

        cleanup_resources();
        return;
    }

    ESP_LOGI(TAG, "=== Application Started Successfully ===");
    ESP_LOGI(TAG, "Display: %dx%d", config.lcd_h_res, config.lcd_v_res);
    ESP_LOGI(TAG, "Backlight: %u%%", config.backlight_level);
    ESP_LOGI(TAG, "Waiting for JSON commands via USB...");

    // Основной цикл с мониторингом состояния
    uint32_t main_loop_counter = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 секунд

        // Периодическая проверка состояния каждую минуту
        if (++main_loop_counter % 6 == 0) {
            ESP_LOGI(TAG, "System status: LVGL=%s, Display=%s",
                     lvgl_disp ? "OK" : "FAIL",
                     disp_panel ? "OK" : "FAIL");
        }
    }
}
