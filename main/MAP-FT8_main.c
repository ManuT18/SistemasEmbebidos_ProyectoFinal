#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_driver.h"
#include "ft8_decode_task.h"

/* 
 * Punto de Entrada de la Aplicación:
 * Inicializa hardware (si no es test) y lanza la tarea de decodificación.
 */
void app_main(void)
{
    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

    // Nota: La inicialización de audio se maneja dentro de ft8_decode_task
    // o se puede hacer aquí si se prefiere centralizar.
    // Por ahora, mantenemos la lógica de TEST_MODE dentro de la tarea para simplicidad.
    
    // Crear Tarea de Decodificación
    xTaskCreate(ft8_decode_task, "ft8_decode", FT8_DECODE_TASK_STACK_SIZE, NULL, FT8_DECODE_TASK_PRIORITY, NULL);
}