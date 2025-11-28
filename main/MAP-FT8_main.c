/**
 * @file MAP-FT8_main.c
 * @brief Punto de entrada de la aplicación MAP-FT8.
 * 
 * Este archivo se encarga de la inicialización básica del sistema y el lanzamiento
 * de las tareas principales (decodificación, y en el futuro transmisión/web).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "audio_driver.h"
#include "ft8_decode_task.h"
#include "web_interface.h"
#include "nvs.h"
#include "ft8_encode_task.h"

// Callbacks para proveer datos a la web
void get_station_callsign(char *buffer, size_t max_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = max_len;
        err = nvs_get_str(my_handle, "callsign", buffer, &required_size);
        nvs_close(my_handle);
    }
    
    if (err != ESP_OK) {
        snprintf(buffer, max_len, "NOCALL");
    }
}

void get_station_grid(char *buffer, size_t max_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = max_len;
        err = nvs_get_str(my_handle, "grid", buffer, &required_size);
        nvs_close(my_handle);
    }
    
    if (err != ESP_OK) {
        snprintf(buffer, max_len, "XX00");
    }
}

void save_station_data(const char *call, const char *grid) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_str(my_handle, "callsign", call);
        nvs_set_str(my_handle, "grid", grid);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI("NVS", "Datos guardados: %s / %s", call, grid);
    } else {
        ESP_LOGE("NVS", "Error al abrir NVS para escritura");
    }
}

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

    // Inicializar Interfaz Web y WiFi AP
    web_interface_init();
    
    // Registrar proveedores de datos
    web_interface_register_data_providers(get_station_callsign, get_station_grid);
    
    // Registrar callback de login (guardado de datos)
    web_interface_set_login_callback(save_station_data);
    
    // Registrar callback para iniciar test
    web_interface_set_test_callback(ft8_start_test);

    // Registrar callback para transmisión CQ
    web_interface_set_tx_cq_callback(ft8_tx_cq);

    // Registrar callbacks para configuración de frecuencia
    web_interface_register_freq_callbacks(ft8_set_tx_freq, ft8_get_tx_freq);

    // Crear Tarea de Decodificación
    xTaskCreate(ft8_decode_task, "ft8_decode", FT8_DECODE_TASK_STACK_SIZE, NULL, FT8_DECODE_TASK_PRIORITY, NULL);
}