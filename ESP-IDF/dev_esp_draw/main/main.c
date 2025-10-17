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

IRAM_ATTR static bool
test_notify_refresh_ready(esp_lcd_panel_handle_t
panel,
esp_lcd_dpi_panel_event_data_t *edata,
void *user_ctx
)
{
SemaphoreHandle_t refresh_finish = (SemaphoreHandle_t) user_ctx;
BaseType_t need_yield = pdFALSE;

xSemaphoreGiveFromISR(refresh_finish,
&need_yield);

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

    refresh_finish = xSemaphoreCreateBinary();

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
            .lane_bit_rate_mbps = 500,
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

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
            .on_color_trans_done = test_notify_refresh_ready,
    };
    esp_lcd_dpi_panel_register_event_callbacks(disp_panel, &cbs, refresh_finish);

}

void fill_background(uint16_t color) {

    uint16_t *full_screen_buffer = (uint16_t *) heap_caps_calloc(
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

void draw_qr(char *data) {
    QRCode qrcode;
    uint8_t qrcodeBytes[qrcode_getBufferSize(QR_VERSION)];

    qrcode_initText(&qrcode, qrcodeBytes, QR_VERSION, QR_ECC_LEVEL, data);

    int qr_total_size = qrcode.size * QR_PIXEL_SIZE;

    // coordinats for centering
    int x_start = (LCD_H_RES - qr_total_size) / 2;
    int y_start = (LCD_V_RES - qr_total_size) / 2;

    // allocate mem
    uint16_t *qr_bitmap = (uint16_t *) heap_caps_calloc(
            qr_total_size * qr_total_size,
            sizeof(uint16_t),
            MALLOC_CAP_SPIRAM
    );
    if (!qr_bitmap) {
        ESP_LOGE(TAG, "Failed to allocate memory for QR bitmap");
        return;
    }

    // filling bitmap image
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            uint16_t color = qrcode_getModule(&qrcode, x, y) ? black_color : white_color;
            for (int dy = 0; dy < QR_PIXEL_SIZE; dy++) {
                for (int dx = 0; dx < QR_PIXEL_SIZE; dx++) {
                    qr_bitmap[(y * QR_PIXEL_SIZE + dy) * qr_total_size + (x * QR_PIXEL_SIZE + dx)] = color;
                }
            }
        }
    }

    // rendering QR
    ESP_LOGI(TAG, "Drawing QR code at (%d, %d)", x_start, y_start);
    esp_lcd_panel_draw_bitmap(
            disp_panel,
            x_start, y_start,
            x_start + qr_total_size,
            y_start + qr_total_size,
            qr_bitmap
    );

    xSemaphoreTake(refresh_finish, portMAX_DELAY);

    // Освобождение памяти
    free(qr_bitmap);
}

// Функция для обработки полученных данных
void process_data(const char *data) {
    ESP_LOGI(TAG, "Получена полная строка: %s", data);

    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Не удалось разобрать JSON");
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *qr_data = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsString(type) && cJSON_IsString(qr_data)) {
        if (strcmp(type->valuestring, "qr") == 0) {
            ESP_LOGI(TAG, "Тип: %s, Данные: %s", type->valuestring, qr_data->valuestring);

            // Здесь ваша логика отрисовки QR-кода на дисплее
            draw_qr(qr_data->valuestring);
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
                    buffer[index++] = (char)c;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {

    init_backlight();

    init_lcd();

    fill_background(white_color);

    char *qr_data = "236299fc-ebb7-42f0-8064-a68d794df943";
    draw_qr(qr_data);

    xTaskCreate(usb_rx_task, "usb_rx_task", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}
