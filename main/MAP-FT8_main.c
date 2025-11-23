#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "audio_driver.h"

#define FT8_TASK_STACK_SIZE 4096
#define FT8_TASK_PRIORITY   5

void ft8_task(void *pvParameters)
{
    ESP_LOGI("FT8_TASK", "Tarea FT8 iniciada");

    // Buffer para procesar audio
    // Usamos el tamaño definido en el driver
    int16_t *audio_buf = (int16_t *)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (audio_buf == NULL) {
        ESP_LOGE("FT8_TASK", "Error al asignar memoria para buffer de audio");
        vTaskDelete(NULL);
    }

    size_t bytes_read = 0;
    size_t bytes_written = 0;

    while (1) {
        // 1. Leer audio del ADC (Bloqueante hasta tener datos)
        // Leemos AUDIO_BUFFER_SIZE muestras
        esp_err_t ret = audio_read(audio_buf, AUDIO_BUFFER_SIZE, &bytes_read);
        
        if (ret == ESP_OK && bytes_read > 0) {
            // Calcular cantidad de muestras leídas (bytes / 2)
            size_t samples_read = bytes_read / sizeof(int16_t);

            // 2. Aquí iría el procesamiento DSP (FT8)
            // ...

            // 3. Loopback: Escribir lo mismo al DAC para probar
            audio_write(audio_buf, samples_read, &bytes_written);
        } else {
            // Pequeño delay si hubo error para no saturar log
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void app_main(void)
{
    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

    // 1. Inicializar Hardware de Audio
    if (audio_driver_init() != ESP_OK) {
        ESP_LOGE("MAP-FT8", "Fallo al inicializar audio. Abortando.");
        return;
    }

    // 2. Crear Tarea Principal de Procesamiento
    xTaskCreate(ft8_task, "ft8_task", FT8_TASK_STACK_SIZE, NULL, FT8_TASK_PRIORITY, NULL);
}