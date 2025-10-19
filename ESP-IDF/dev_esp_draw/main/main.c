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
static const char *TAG = "esp_draw_bit";
// #define BSP_I2C_SCL           (GPIO_NUM_8)
// #define BSP_I2C_SDA           (GPIO_NUM_7)
#define LCD_BACKLIGHT     (GPIO_NUM_23)
#define LCD_RST           (GPIO_NUM_5)
// #define LCD_TOUCH_RST     (GPIO_NUM_22)
// #define LCD_TOUCH_INT     (GPIO_NUM_21)
#define LCD_H_RES         (480)
#define LCD_V_RES         (800)
#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)    // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1500) // 1Gbps
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)
// static i2c_master_bus_handle_t i2c_handle = NULL; 
// static esp_lcd_touch_handle_t ret_touch;
static SemaphoreHandle_t refresh_finish = NULL;
static esp_lcd_panel_handle_t disp_panel = NULL;
static lv_disp_draw_buf_t disp_buf;
static lv_disp_t *lvgl_disp;
static lv_color_t *buf1;
//#define LVGL_BUFFER_SIZE (LCD_H_RES * LCD_V_RES / 5)
#define LVGL_BUFFER_SIZE (LCD_H_RES * LCD_V_RES)
// Глобальные переменные для объектов LVGL
lv_obj_t *qr_obj = NULL;
lv_obj_t *label_obj = NULL;

IRAM_ATTR static bool test_notify_refresh_ready(esp_lcd_panel_handle_t panel,
                                                esp_lcd_dpi_panel_event_data_t *edata,
                                                void *user_ctx)
{
//    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    BaseType_t need_yield = pdFALSE;

    // Сообщаем LVGL, что отрисовка завершена
//    lv_disp_flush_ready(drv);

    // Пробуждаем задачу, которая ждет, если она есть (не в вашем случае, но правильно)
    xSemaphoreGiveFromISR(refresh_finish, &need_yield);
    return (need_yield == pdTRUE);
}
static esp_err_t bsp_enable_dsi_phy_power(void) {
#if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, "Acquire LDO channel for DPHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
    return ESP_OK;
}
const uint16_t white_color = 0xFFFF;
const uint16_t black_color = 0x0000;
#define USE_PWM                 1
#define BACKLIGHT_LEVEL         20
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RESOLUTION    LEDC_TIMER_10_BIT
#define LEDC_FREQUENCY          (25000) // 25 KHz
static SemaphoreHandle_t lvgl_mutex = NULL;
// Определяем максимальный размер буфера для QR-кода.
// Версия 3 с высоким уровнем коррекции ошибок (ECC_HIGH).
#define QR_VERSION 5
#define QR_ECC_LEVEL ECC_HIGH
#define QR_PIXEL_SIZE 12 // Размер одного модуля QR-кода в пикселях
#define USB_RX_BUFFER_SIZE 256 // Или больше, в зависимости от размера JSON

void init_backlight(void) {
    if (USE_PWM == 0) {
        ESP_LOGI(TAG, "Turn on LCD backlight without PWM");
        // turn on backlight 100% w/o pwm
        gpio_set_level(LCD_BACKLIGHT, 1);
        return;
    }
    ESP_LOGI(TAG, "Turn on LCD backlight using PWM");
    gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
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
            .duty           = 0, // Начальная яркость 0 (подсветка выключена)
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
            .bus_id = 0,
            .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = 1500,
    };
    esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_config = {
            .virtual_channel = 0,
            .lcd_cmd_bits = 8,   // according to the LCD ILI9881C spec
            .lcd_param_bits = 8, // according to the LCD ILI9881C spec
    };
    esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);
//    esp_lcd_dpi_panel_config_t dpi_config = ST7701_480_360_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    esp_lcd_dpi_panel_config_t dpi_config = ST7701_480_360_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    st7701_vendor_config_t vendor_config = {
            .mipi_config = {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
            },
            .flags = {
                    .use_mipi_interface = 1,
            }
    };
    esp_lcd_panel_dev_config_t lcd_dev_config = {
            .bits_per_pixel = 16,
            .rgb_ele_order = ESP_LCD_COLOR_SPACE_RGB,
            .reset_gpio_num = LCD_RST,
            .vendor_config = &vendor_config,
    };
    esp_lcd_new_panel_st7701(io, &lcd_dev_config, &disp_panel);
    esp_lcd_panel_reset(disp_panel);
    esp_lcd_panel_init(disp_panel);
    esp_lcd_panel_disp_on_off(disp_panel, true);
//    esp_lcd_dpi_panel_event_callbacks_t cbs = {
//            .on_color_trans_done = test_notify_refresh_ready,
//    };
//    esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, lvgl_disp);

}
void fill_background(uint16_t color) {
    uint16_t *full_screen_buffer = (uint16_t * )
    heap_caps_calloc(
            LCD_H_RES * LCD_V_RES,
            sizeof(uint16_t),
            MALLOC_CAP_SPIRAM
    );
    if (!full_screen_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for full screen buffer");
        return;
    }
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        full_screen_buffer[i] = color;
    }
    esp_lcd_panel_draw_bitmap(disp_panel, 0, 0, LCD_H_RES, LCD_V_RES, full_screen_buffer);
    xSemaphoreTake(refresh_finish, portMAX_DELAY);
    // Освобождение буфера, так как он больше не нужен
    free(full_screen_buffer);
}
static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    ESP_LOGI(TAG, "lv_flush_cb called: x1=%d, y1=%d, x2=%d, y2=%d", area->x1, area->y1, area->x2, area->y2);

    esp_lcd_panel_draw_bitmap(
            disp_panel,
            area->x1,
            area->y1,
            area->x2 + 1,
            area->y2 + 1,
            color_p
    );

    // Ждем, пока прерывание (ISR) сообщит нам, что DMA-передача завершена.
    // Эта строка блокирует выполнение задачи до тех пор, пока семафор не будет отдан.
    xSemaphoreTake(refresh_finish, portMAX_DELAY);

    // Теперь, когда отрисовка гарантированно завершена, сообщаем об этом LVGL.
    // Это разблокирует логику внутри LVGL для отрисовки следующей части экрана.
    lv_disp_flush_ready(drv);
}
void lvgl_task(void *pvParameter) {
    while (1) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void draw_qr_lvgl(const char *data) {
    if (lv_scr_act() == NULL) {
        ESP_LOGE(TAG, "LVGL не инициализирован. Невозможно создать QR-код.");
        return;
    }
    if (qr_obj == NULL) {
        ESP_LOGI(TAG, "draw_qr_lvgl: qr_obj is null. creating...");
        qr_obj = lv_qrcode_create(lv_scr_act(), 150, lv_color_black(), lv_color_white());
        lv_obj_center(qr_obj);
    }
    // Убедиться, что текст скрыт
    if (label_obj != NULL) {
        ESP_LOGI(TAG, "draw_qr_lvgl: qr_obj created");
        lv_obj_add_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "draw_qr_lvgl: qr_obj lv_obj_add_flag LV_OBJ_FLAG_HIDDEN");
    }
    if (strlen(data) > 0) {
        lv_qrcode_update(qr_obj, data, strlen(data));
        lv_obj_clear_flag(qr_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "draw_qr_lvgl: qr_obj lv_obj_clear_flag LV_OBJ_FLAG_HIDDEN");
    } else {
        lv_obj_add_flag(qr_obj, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "draw_qr_lvgl: qr_obj lv_obj_add_flag LV_OBJ_FLAG_HIDDEN");
    }
    lv_refr_now(lvgl_disp);
}
void display_text_lvgl(const char *text) {
    if (lv_scr_act() == NULL) {
        ESP_LOGE(TAG, "LVGL не инициализирован. Невозможно отобразить текст.");
        return;
    }
    if (label_obj == NULL) {
        label_obj = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_align(label_obj, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label_obj);
    }
    lv_label_set_text(label_obj, text);
    lv_obj_clear_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
    // Убедиться, что QR-код скрыт
    if (qr_obj != NULL) {
        lv_obj_add_flag(qr_obj, LV_OBJ_FLAG_HIDDEN);
    }
    lv_refr_now(lvgl_disp);
}
void async_draw_qr(void *data) {
    char *qr_data_str = (char *) data;
    ESP_LOGI(TAG, "async_draw_qr: Starting LVGL QR creation");
//    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
//    lv_obj_clean(lv_scr_act());
    draw_qr_lvgl(qr_data_str);
//    xSemaphoreGive(lvgl_mutex);
    ESP_LOGI(TAG, "async_draw_qr: Finished LVGL QR creation");
    free(qr_data_str); // Освободите память после использования
}
void async_display_text(void *data) {
    char *text_data = (char *) data;

//    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);

    display_text_lvgl(text_data);

//    xSemaphoreGive(lvgl_mutex);

    free(text_data); // Освободите память после использования
}

// Функция для обработки полученных данных
void process_data(const char *data) {
    ESP_LOGI(TAG, "Получена полная строка: %s", data);
    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Не удалось разобрать JSON");
        return;
    }
    cJSON *type    = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsString(type) && cJSON_IsString(content)) {
        ESP_LOGI(TAG, "Тип: %s, Данные: %s", type->valuestring, content->valuestring);
        char *content_copy = strdup(content->valuestring);
        if (content_copy == NULL) {
            cJSON_Delete(root);
            return;
        }
        if (strcmp(type->valuestring, "qr") == 0) {
            ESP_LOGI(TAG, "Отправка команды на отрисовку QR");
            lv_async_call(async_draw_qr, content_copy);
        } else if (strcmp(type->valuestring, "text") == 0) {
            ESP_LOGI(TAG, "Отправка команды на отрисовку текста");
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
    refresh_finish = xSemaphoreCreateBinary();
    init_lcd();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Инициализация LVGL
    lv_init();

    uint32_t buf_size = LCD_H_RES * 100; // Рекомендуется выделять буфер на несколько строк
    buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf1 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
        return;
    }

    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LCD_H_RES);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lv_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lvgl_disp = lv_disp_drv_register(&disp_drv);

    // !!! ВОТ ЭТО НУЖНО ДОБАВИТЬ ЗДЕСЬ !!!
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
            .on_color_trans_done = test_notify_refresh_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, NULL));

    lv_obj_clean(lv_scr_act());
    // --- Тестовый код ---
    lv_obj_t *test_label = lv_label_create(lv_scr_act());
    lv_label_set_text(test_label, "LVGL OK");
    lv_obj_center(test_label);
// --------------------
//    char *data = "123";
//    qr_obj = lv_qrcode_create(lv_scr_act(), 150, lv_color_black(), lv_color_white());
//    lv_obj_center(qr_obj);
//    lv_qrcode_update(qr_obj, data, strlen(data));

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);
    init_backlight();
// {"type": "qr", "data": "https://example.com"}
// {"type": "qr", "data": "123"}
// char *qr_data = "236299fc-ebb7-42f0-8064-a68d794df943";
// https://www.meme-arsenal.com/memes/648da849201ad7d325466cc03afb2703.jpg

    lvgl_mutex = xSemaphoreCreateMutex();
    // Запуск LVGL-задачи
    xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);
//    xTaskCreate(qr_task, "qr_task", 4096, NULL, 5, NULL);
    // Запуск задачи для чтения данных с USB
    xTaskCreate(usb_rx_task, "usb_rx_task", 4096, NULL, 5, NULL);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
