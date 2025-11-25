#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_interface.h"
#include "audio_driver.h"
#include "ft8_decode_task.h"

/* 
 * Punto de Entrada de la Aplicación:
 * Inicializa hardware (si no es test) y lanza la tarea de decodificación.
 */
void app_main(void)
{
    // Inicializar NVS (Requerido para WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

    // Inicializar Interfaz Web
    web_interface_init();

    // Nota: La inicialización de audio se maneja dentro de ft8_decode_task
    // o se puede hacer aquí si se prefiere centralizar.
    // Por ahora, mantenemos la lógica de TEST_MODE dentro de la tarea para simplicidad.
    
    // Crear Tarea de Decodificación
    xTaskCreate(ft8_decode_task, "ft8_decode", FT8_DECODE_TASK_STACK_SIZE, NULL, FT8_DECODE_TASK_PRIORITY, NULL);
}