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
#include "qrcode.h"
#include "cJSON.h"
#include "lvgl.h"

#define LCD_BACKLIGHT                              (GPIO_NUM_23)
#define LCD_RST                                    (GPIO_NUM_5)
#define LCD_H_RES                                  (480)
#define LCD_V_RES                                  (800)
#define BSP_LCD_MIPI_DSI_LANE_NUM                  (2)         // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS         (1500)      // 1Gbps
#define LANE_BITRATE_MBPS                          500
#define V_TASK_DELAY                               10
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN              (3)         // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV        (2500)
#define LVGL_BUFFER_SIZE                           (LCD_H_RES * LCD_V_RES)
#define USE_PWM_BACKLIGHT                          1
#define BACKLIGHT_LEVEL                            20
#define LEDC_TIMER                                 LEDC_TIMER_0
#define LEDC_MODE                                  LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL                               LEDC_CHANNEL_0
#define LEDC_DUTY_RESOLUTION                       LEDC_TIMER_10_BIT
#define LEDC_FREQUENCY                             (25000) // 25 KHz
#define USB_RX_BUFFER_SIZE                         256
#define QR_SIZE                                    450

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

static const char *TAG                           = "esp_draw_bit";
const uint16_t  white_color                      = 0xFFFF;
const uint16_t black_color                       = 0x0000;
static         esp_lcd_panel_handle_t disp_panel = NULL;
static         lv_disp_draw_buf_t     disp_buf;
static         lv_disp_t              *lvgl_disp;
static         lv_color_t             *buf1;
static         lv_color_t             *buf2;
static         SemaphoreHandle_t      lvgl_mutex = NULL;

lv_obj_t       *qr_obj    = NULL;
lv_obj_t       *label_obj = NULL;

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
    if (USE_PWM_BACKLIGHT == 0) {
        ESP_LOGI(TAG, "Turn on LCD backlight without PWM");
        // turn on backlight 100% w/o pwm
        gpio_set_level(LCD_BACKLIGHT, 1);
        return;
    }

    ESP_LOGI(TAG, "Turn on LCD backlight using PWM");
    gpio_config_t bk_gpio_config = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << LCD_BACKLIGHT
    };
    gpio_config(&bk_gpio_config);

    ledc_timer_config_t ledc_timer = {
            .speed_mode       = LEDC_MODE,
            .duty_resolution  = LEDC_DUTY_RESOLUTION,
            .timer_num        = LEDC_TIMER,
            .freq_hz          = LEDC_FREQUENCY,
            .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CHANNEL,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = LCD_BACKLIGHT,
            .duty           = 0,
            .hpoint         = 0,
    };
    ledc_channel_config(&ledc_channel);

    uint32_t duty = (((1 << LEDC_DUTY_RESOLUTION) - 1) / 100) * BACKLIGHT_LEVEL;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);

    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void init_lcd(void) {
    bsp_enable_dsi_phy_power();
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id             = 0,
            .num_data_lanes     = BSP_LCD_MIPI_DSI_LANE_NUM,
            .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = LANE_BITRATE_MBPS,
    };
    esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    esp_lcd_panel_io_handle_t   io;
    esp_lcd_dbi_io_config_t     dbi_config = {
            .virtual_channel    = 0,
            .lcd_cmd_bits       = 8,
            .lcd_param_bits     = 8,
    };
    esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);

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
    esp_lcd_new_panel_st7701(io, &lcd_dev_config, &disp_panel);

    esp_lcd_panel_reset(disp_panel);
    esp_lcd_panel_init(disp_panel);
    esp_lcd_panel_disp_on_off(disp_panel, true);

}

static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
//    ESP_LOGI(TAG, "🎨 Flush: %d,%d → %d,%d", area->x1, area->y1, area->x2, area->y2);
    int w = (area->x2 - area->x1 + 1);
    int h = (area->y2 - area->y1 + 1);
    ESP_LOGI(TAG, "🎨 Flush area: %d,%d | %dx%d", area->x1, area->y1, w, h);

    if (w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "Invalid flush area!");
        lv_disp_flush_ready(drv);
        return;
    }

    esp_lcd_panel_draw_bitmap(disp_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
}

void lvgl_task(void *pvParameter) {
    while (1) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(V_TASK_DELAY));
    }
}

void lv_label_hide(){
    if (label_obj != NULL) {
        lv_obj_add_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "label hide");
    }else{
        ESP_LOGI(TAG, "label hide err. label_obj is NULL");
    }
}

void lv_label_show(){
    if (label_obj != NULL) {
        lv_obj_clear_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "label show");
    }else{
        ESP_LOGI(TAG, "label show err. label_obj is NULL");
    }
}

void lv_qr_hide(){
    if (qr_obj != NULL) {
        lv_obj_add_flag(qr_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "qr hide");
    }else{
        ESP_LOGI(TAG, "qr hide err. qr_obj is NULL");
    }
}

void lv_qr_show(){
    if (qr_obj != NULL) {
        lv_obj_clear_flag(qr_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "qr show");
    }else{
        ESP_LOGI(TAG, "qr show err. qr_obj is NULL");
    }
}

void draw_qr_lvgl(const char *data) {
    if (lv_scr_act() == NULL) return;
    char *local_TAG = "draw_qr_lvgl";

    if (lv_scr_act() == NULL) {
        ESP_LOGI(TAG,"%s: LVGL not initialized. Can't create QR-code.", local_TAG);
        return;
    }

//    lv_obj_clean(lv_scr_act()); // ← full clean display

    lv_label_hide();

    if (qr_obj == NULL) {
        ESP_LOGI(TAG,"%s: qr_obj is null. creating...", local_TAG);

        // https://docs.lvgl.io/master/details/libs/qrcode.html
        // https://docs.lvgl.io/8.4/libs/qrcode.html#example
        qr_obj = lv_qrcode_create(lv_scr_act(), QR_SIZE, lv_color_black(), lv_color_white());
        if (qr_obj == NULL) {
            ESP_LOGI(TAG,"%s: ❌ returned NULL! Check LV_USE_QRCODE in lv_conf.h", local_TAG);
            return;
        }
    }
// {"type": "qr", "data": "123"}
    lv_obj_t * scr = lv_scr_act();
    ESP_LOGI(TAG, "Screen size: %d x %d", lv_obj_get_width(scr), lv_obj_get_height(scr));

    lv_obj_center(qr_obj);

    lv_obj_set_style_border_color(qr_obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(qr_obj, 2, 0);

    ESP_LOGI(TAG, "QR size set to: %d", QR_SIZE);
    ESP_LOGI(TAG, "QR actual size: %d x %d",
             lv_obj_get_width(qr_obj), lv_obj_get_height(qr_obj));
    ESP_LOGI(TAG,"%s: ✅ QR object created successfully", local_TAG);


    if (data && strlen(data) > 0) {
        lv_qrcode_update(qr_obj, data, strlen(data));
        lv_qr_show();
        lv_obj_invalidate(qr_obj);
        ESP_LOGI(TAG, "QR updated. Final size: %d x %d",
                 lv_obj_get_width(qr_obj), lv_obj_get_height(qr_obj));
    } else {
        lv_qr_hide();
    }
}

void display_text_lvgl(const char *text) {
    char *local_TAG = "display_text_lvgl";
    if (lv_scr_act() == NULL) {
        ESP_LOGI(TAG,"%s: LVGL not initialized. Can't render text label.", local_TAG);
        return;
    }

    if (label_obj == NULL) {
        ESP_LOGI(TAG,"%s: label_obj is null. creating...", local_TAG);

        label_obj     = lv_label_create(lv_scr_act());
        if (label_obj == NULL) {
            ESP_LOGI(TAG,"%s: ❌ lv_label_create returned NULL!", local_TAG);
            return;
        }

        ESP_LOGI(TAG,"%s: ✅ Label object created successfully", local_TAG);
        lv_obj_set_width(label_obj, LV_PCT(90));
        lv_obj_align(label_obj, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(label_obj, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label_obj);
        lv_color_t indigo_color = lv_palette_main(LV_PALETTE_INDIGO);
        lv_obj_set_style_text_color(label_obj, indigo_color, 0);
    }

    if (qr_obj) lv_qr_hide();

    if (text == NULL) text = "";

    if (strlen(text) > 0) {
        lv_label_set_text(label_obj, text);
        lv_label_show();
    } else {
        lv_label_hide();
    }
}

void async_draw_qr(void *data) {
    char *local_TAG = "async_draw_qr";
    char *qr_data_str = (char *) data;

    ESP_LOGI(TAG,"%s: Starting LVGL QR creation", local_TAG);

    draw_qr_lvgl(qr_data_str);

//    lv_refr_now(lvgl_disp);

    if (lvgl_disp) {
        lv_disp_trig_activity(lvgl_disp); // ← помогает при "застое"
    }

    ESP_LOGI(TAG,"%s: Finished LVGL QR creation", local_TAG);

    free(qr_data_str);
}

void async_display_text(void *data) {
    char *local_TAG = "async_display_text";
    char *text_data = (char *) data;

    ESP_LOGI(TAG,"%s: Starting LVGL QR creation", local_TAG);

    display_text_lvgl(text_data);

//    lv_refr_now(lvgl_disp);

    ESP_LOGI(TAG,"%s: Finished LVGL QR creation", local_TAG);

    free(text_data);
}

void process_data(const char *data) {
    ESP_LOGI(TAG, "Received: %s", data);

    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        ESP_LOGE(TAG, "failed to parse string as JSON");
        return;
    }

    cJSON *type    = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsString(type) && cJSON_IsString(content)) {
        ESP_LOGI(TAG, "Type: %s, Data: %s", type->valuestring, content->valuestring);

        char *content_copy = strdup(content->valuestring);
        if (content_copy == NULL) {
            ESP_LOGE(TAG, "failed to parse string as JSON");
            cJSON_Delete(root);
            return;
        }
        if (strcmp(type->valuestring, "qr") == 0) {
            ESP_LOGI(TAG, "Sending a QR rendering command");
//            lv_async_call(async_draw_qr, content_copy);

            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            draw_qr_lvgl(content_copy); // ← теперь process_data вызывает draw_qr_lvgl напрямую
            xSemaphoreGive(lvgl_mutex);

        } else if (strcmp(type->valuestring, "text") == 0) {
            ESP_LOGI(TAG, "Sending a text rendering command");
            lv_async_call(async_display_text, content_copy);
        } else {
            free(content_copy); // Освобождаем, если не использовали
        }
    }
    cJSON_Delete(root);
}

void usb_rx_task(void *arg) {
    char buffer[USB_RX_BUFFER_SIZE];
    int index = 0;
    while (1) {
        int c = fgetc(stdin);
        if (c != EOF) {
            if (c == '\n' || c == '\r') {
                if (index > 0) {
                    buffer[index] = '\0';
                    process_data(buffer);
                }
                index = 0;
            } else {
                if (index < sizeof(buffer) - 1) {
                    buffer[index++] = (char) c;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {

    #ifdef LV_USE_QRCODE
        ESP_LOGI(TAG, "✅ LV_USE_QRCODE is ENABLED");
    #else
        ESP_LOGE(TAG, "❌ LV_USE_QRCODE is DISABLED");
    #endif

    init_lcd();
    vTaskDelay(pdMS_TO_TICKS(10));

    lv_init();

    uint32_t buf_size = LCD_H_RES * 250;
    buf1              = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2              = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");

        if (buf1) free(buf1);
        if (buf2) free(buf2);
        return;
    }

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_size);

    lv_disp_drv_t     disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_H_RES;
    disp_drv.ver_res  = LCD_V_RES;
    disp_drv.flush_cb = lv_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lvgl_disp         = lv_disp_drv_register(&disp_drv);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
            .on_color_trans_done = test_notify_refresh_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, &disp_drv));

//    lv_obj_clean(lv_scr_act());

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
    init_backlight();

    // --- Тестовый код ---
//    test_label = lv_label_create(lv_scr_act());
//    lv_obj_set_style_text_align(test_label, LV_TEXT_ALIGN_CENTER, 0);
//    lv_obj_add_style(test_label, &my_style, 0);
//    lv_label_set_text(test_label, "LVGL OK");
//    lv_obj_center(test_label);


//    lv_color_t bg_color = lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 5);
//    lv_color_t fg_color = lv_palette_darken(LV_PALETTE_BLUE, 4);
//
//    lv_obj_t * qr = lv_qrcode_create(lv_scr_act(), QR_SIZE, fg_color, bg_color);
//
//    /*Set data*/
//    const char * data = "https://lvgl.io";
//    lv_qrcode_update(qr, data, strlen(data));
//    lv_obj_center(qr);
//
//    /*Add a border with bg_color*/
//    lv_obj_set_style_border_color(qr, bg_color, 0);
//    lv_obj_set_style_border_width(qr, 5, 0);


// --------------------
//    char *data = "123";
//    qr_obj = lv_qrcode_create(lv_scr_act(), 150, lv_color_black(), lv_color_white());
//    lv_obj_center(qr_obj);
//    lv_qrcode_update(qr_obj, data, strlen(data));

    /*
     {"type": "qr", "data": "https://example.com"}
     {"type": "qr", "data": "https://www.meme-arsenal.com/memes/648da849201ad7d325466cc03afb2703.jpg"}
     {"type": "qr", "data": "123"}
     {"type": "text", "data": "123"}
     {"type": "text", "data": "abc"}
     char *qr_data = "236299fc-ebb7-42f0-8064-a68d794df943";
     https://www.meme-arsenal.com/memes/648da849201ad7d325466cc03afb2703.jpg
    */

    lvgl_mutex = xSemaphoreCreateMutex();

    xTaskCreate(lvgl_task,  "lvgl_task", 4096, NULL, 5, NULL);
    xTaskCreate(usb_rx_task, "usb_rx_task", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
