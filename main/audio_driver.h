#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Configuración de Audio para FT8
// FT8 usa ~50Hz a ~3000Hz. 
// Sample rate: 12000 Hz es común y suficiente (Nyquist > 6000Hz).
#define AUDIO_SAMPLE_RATE 12000
#define AUDIO_BUFFER_SIZE 1024 // Cantidad de muestras por buffer

/**
 * @brief Inicializa el sistema de audio (ADC para RX, DAC para TX)
 * 
 * @return esp_err_t ESP_OK si tuvo éxito
 */
esp_err_t audio_driver_init(void);

/**
 * @brief Lee muestras de audio del ADC (Bloqueante)
 * 
 * @param buffer Puntero al buffer donde guardar las muestras
 * @param length Cantidad de muestras a leer
 * @param bytes_read Puntero para guardar la cantidad de bytes leídos
 * @return esp_err_t ESP_OK si tuvo éxito
 */
esp_err_t audio_read(int16_t *buffer, size_t length, size_t *bytes_read);

/**
 * @brief Escribe muestras de audio al DAC (Bloqueante)
 * 
 * @param buffer Puntero al buffer con las muestras a escribir
 * @param length Cantidad de muestras a escribir
 * @param bytes_written Puntero para guardar la cantidad de bytes escritos
 * @return esp_err_t ESP_OK si tuvo éxito
 */
esp_err_t audio_write(const int16_t *buffer, size_t length, size_t *bytes_written);

#endif // AUDIO_DRIVER_H
