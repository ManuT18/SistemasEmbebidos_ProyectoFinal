/**
 * @file MAP-FT8_main.c
 * @brief Punto de entrada de la aplicación MAP-FT8.
 * 
 * Este archivo se encarga de la inicialización básica del sistema y el lanzamiento
 * de las tareas principales (decodificación, y en el futuro transmisión/web).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "audio_driver.h"
#include "ft8_decode_task.h"

// TODO: Incluir headers de web_interface cuando esté implementado
// #include "web_interface.h"

/**
 * @brief Función principal de la aplicación (Entry Point).
 * 
 * Inicializa el hardware necesario (NVS, etc.) y crea la tarea de decodificación FT8.
 */
void app_main(void)
{
    // Inicializar NVS (Requerido para WiFi y configuraciones futuras)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

    /* 
     * Nota: La inicialización de audio se maneja dentro de ft8_decode_task
     * para permitir el modo de prueba (WAV) sin inicializar hardware innecesario.
     */
    
    // Crear Tarea de Decodificación
    xTaskCreate(ft8_decode_task, "ft8_decode", FT8_DECODE_TASK_STACK_SIZE, NULL, FT8_DECODE_TASK_PRIORITY, NULL);
}