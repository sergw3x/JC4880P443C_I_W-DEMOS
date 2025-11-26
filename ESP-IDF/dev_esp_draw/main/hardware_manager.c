#include "hardware_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_st7701.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hardware_manager";

// LCD Hardware Configuration
#define LCD_BACKLIGHT                              (GPIO_NUM_23)
#define LCD_RST                                    (GPIO_NUM_5)

// MIPI DSI Configuration
#define BSP_LCD_MIPI_DSI_LANE_NUM                  (2)         // 2 data lanes
#define LANE_BITRATE_MBPS                          500         // Lane bit rate in Mbps

// Power Management
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN              (3)         // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV        (2500)      // LDO voltage in millivolts

// Backlight Configuration
#define USE_PWM_BACKLIGHT                          1           // Use PWM for backlight control (0=off, 1=on)
#define BACKLIGHT_LEVEL                            20          // Backlight brightness level (0-100%)
#define LEDC_TIMER                                 LEDC_TIMER_0
#define LEDC_MODE                                  LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL                               LEDC_CHANNEL_0
#define LEDC_DUTY_RESOLUTION                       LEDC_TIMER_10_BIT  // 10-bit resolution (0-1023)
#define LEDC_FREQUENCY                             (25000)     // PWM frequency in Hz (25 KHz)

// Forward declarations
static esp_err_t bsp_enable_dsi_phy_power(void);
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

// Global panel handle (экспортируется для других модулей)
esp_lcd_panel_handle_t disp_panel = NULL;

esp_err_t init_hardware(void) {
    ESP_LOGI(TAG, "=== Initializing Hardware ===");
    
    // Initialize LCD
    init_lcd();
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize backlight
    init_backlight();
    
    ESP_LOGI(TAG, "=== Hardware initialization completed ===");
    return ESP_OK;
}

void init_lcd(void) {
    esp_err_t ret;
    
    // Enable DSI PHY power
    ret = bsp_enable_dsi_phy_power();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable DSI PHY power: %s", esp_err_to_name(ret));
        // Continue without DSI PHY for now
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
        return;
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
        return;
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
        return;
    }

    // Reset panel
    ret = esp_lcd_panel_reset(disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset LCD panel: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize panel
    ret = esp_lcd_panel_init(disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
        return;
    }
    
    // Turn on display
    ret = esp_lcd_panel_disp_on_off(disp_panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn on LCD display: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "LCD initialization completed successfully");
}

void init_backlight(void) {
    esp_err_t ret;
    
    if (USE_PWM_BACKLIGHT == false) {
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
    uint32_t duty = (((1 << LEDC_DUTY_RESOLUTION) - 1) / 100) * BACKLIGHT_LEVEL;
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
    
    ESP_LOGI(TAG, "Backlight initialized successfully at %d%% brightness", BACKLIGHT_LEVEL);
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





