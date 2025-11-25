#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_interface.h"
#include "audio_driver.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_interface.h"
#include "audio_driver.h"
#include "ft8_decode_task.h"

// Variables globales para datos de estación
char station_callsign[16] = "NOCALL";
char station_grid[8] = "XX00";

// Callback cuando el usuario se loguea desde la web
void on_web_login(const char *call, const char *grid)
{
    snprintf(station_callsign, sizeof(station_callsign), "%s", call);
    snprintf(station_grid, sizeof(station_grid), "%s", grid);
    ESP_LOGI("MAP-FT8", "Datos de estación actualizados: %s @ %s", station_callsign, station_grid);
}

// Proveedores de datos para la web
void provide_callsign(char *buffer, size_t max_len)
{
    snprintf(buffer, max_len, "%s", station_callsign);
}

void provide_grid(char *buffer, size_t max_len)
{
    snprintf(buffer, max_len, "%s", station_grid);
}

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
    web_interface_set_login_callback(on_web_login);
    web_interface_register_data_providers(provide_callsign, provide_grid);

    // Nota: La inicialización de audio se maneja dentro de ft8_decode_task
    // o se puede hacer aquí si se prefiere centralizar.
    // Por ahora, mantenemos la lógica de TEST_MODE dentro de la tarea para simplicidad.
    
    // Crear Tarea de Decodificación
    xTaskCreate(ft8_decode_task, "ft8_decode", FT8_DECODE_TASK_STACK_SIZE, NULL, FT8_DECODE_TASK_PRIORITY, NULL);
}