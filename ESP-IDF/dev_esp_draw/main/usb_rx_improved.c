#include "freertos/FreeRTOS.h"
#include "freertos/task.h" 
#include "esp_heap_caps.h"
#include "esp_log.h"

// Определение констант для улучшенной обработки USB
#define USB_RX_BUFFER_SIZE                         2048        // USB input buffer size in bytes  
#define USB_COMMAND_TIMEOUT_MS                     10000       // Timeout for incomplete commands

// Глобальные переменные (из main.c)
extern const char *TAG;
extern void process_data(const char *data);

// Улучшенная функция для обработки USB ввода с поддержкой PSRAM и длинных команд
void usb_rx_task_improved(void *arg) {
    // Используем PSRAM для больших буферов, если доступен
    char *buffer = NULL;
    
    // Сначала пробуем выделить в PSRAM, затем в обычной RAM
    buffer = heap_caps_malloc(USB_RX_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (buffer == NULL) {
        ESP_LOGW(TAG, "PSRAM not available or insufficient space, using internal RAM");
        buffer = heap_caps_malloc(USB_RX_BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    }
    
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate USB buffer");
        vTaskDelete(NULL);
        return;
    }
    
    // Информация о буфере
    ESP_LOGI(TAG, "USB RX buffer allocated: %d bytes (PSRAM: %s)", 
             USB_RX_BUFFER_SIZE, 
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0 ? "yes" : "no");
    
    int index = 0;
    uint32_t last_activity_time = xTaskGetTickCount();
    
    // Initialize buffer to prevent garbage data
    memset(buffer, 0, USB_RX_BUFFER_SIZE);
    
    while (1) {
        int c = fgetc(stdin);
        
        if (c != EOF) {
            last_activity_time = xTaskGetTickCount();
            
            // Проверяем завершение команды
            if (c == '\n' || c == '\r') {
                // Пропускаем лишние переносы строк
                if (index == 0) {
                    continue;
                }
                
                buffer[index] = '\0';
                
                ESP_LOGI(TAG, "USB command received: %d bytes", index);
                // Выводим начало команды для отладки
                if (index > 100) {
                    ESP_LOGI(TAG, "Command preview: %.100s...", buffer);
                } else {
                    ESP_LOGI(TAG, "Command full: %s", buffer);
                }
                
                // Валидация длины данных перед обработкой
                if (index < USB_RX_BUFFER_SIZE - 1) {
                    process_data(buffer);
                } else {
                    ESP_LOGW(TAG, "USB data too long (%d bytes), discarding", index);
                }
                
                // Сброс буфера и индекса
                memset(buffer, 0, USB_RX_BUFFER_SIZE);
                index = 0;
            } else {
                // Добавляем символ к буферу
                if (index < USB_RX_BUFFER_SIZE - 2) { // Резервируем место для null terminator
                    buffer[index++] = (char) c;
                } else {
                    // Защита от переполнения - очистка и сброс
                    ESP_LOGW(TAG, "USB buffer overflow detected (max %d bytes), resetting", USB_RX_BUFFER_SIZE);
                    ESP_LOGW(TAG, "Overflow command prefix: %.50s", buffer);
                    
                    memset(buffer, 0, USB_RX_BUFFER_SIZE);
                    index = 0;
                }
            }
        } else {
            // Проверяем таймаут для неполных команд
            uint32_t current_time = xTaskGetTickCount();
            uint32_t time_diff_ms = (current_time - last_activity_time) * portTICK_PERIOD_MS;
            
            if (index > 0 && time_diff_ms > USB_COMMAND_TIMEOUT_MS) {
                ESP_LOGW(TAG, "USB command timeout (%lu ms), discarding %d incomplete bytes", 
                         USB_COMMAND_TIMEOUT_MS, index);
                ESP_LOGW(TAG, "Incomplete command: %.100s", buffer);
                
                // Сброс неполной команды
                memset(buffer, 0, USB_RX_BUFFER_SIZE);
                index = 0;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Небольшая задержка для снижения нагрузки на CPU
    }
    
    // Очистка ресурсов (никогда не достигается в нормальных условиях)
    if (buffer != NULL) {
        heap_caps_free(buffer);
    }
}
