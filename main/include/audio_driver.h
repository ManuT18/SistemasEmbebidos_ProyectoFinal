/**
 * @file audio_driver.h
 * @brief Driver de Audio para ESP32 (ADC/DAC).
 * 
 * Proporciona una interfaz abstracta para inicializar y utilizar el hardware de audio.
 * Soporta lectura desde ADC (I2S/ADC built-in) y escritura a DAC (I2S/DAC built-in).
 */

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
 * @brief Inicializa el sistema de audio (ADC para RX, DAC para TX).
 * 
 * Configura el I2S en modo ADC/DAC built-in.
 * 
 * @return esp_err_t ESP_OK si tuvo éxito.
 */
esp_err_t audio_driver_init(void);

/**
 * @brief Lee muestras de audio del ADC (Bloqueante).
 * 
 * @param[out] buffer Puntero al buffer donde guardar las muestras.
 * @param[in] length Cantidad de muestras a leer.
 * @param[out] bytes_read Puntero para guardar la cantidad de bytes leídos.
 * @return esp_err_t ESP_OK si tuvo éxito.
 */
esp_err_t audio_read(int16_t *buffer, size_t length, size_t *bytes_read);

/**
 * @brief Escribe muestras de audio al DAC (Bloqueante).
 * 
 * @param[in] buffer Puntero al buffer con las muestras a escribir.
 * @param[in] length Cantidad de muestras a escribir.
 * @param[out] bytes_written Puntero para guardar la cantidad de bytes escritos.
 * @return esp_err_t ESP_OK si tuvo éxito.
 */
esp_err_t audio_write(const int16_t *buffer, size_t length, size_t *bytes_written);

/**
 * @brief Prepara el driver para transmisión (Detiene ADC, Inicia DAC).
 * @return esp_err_t ESP_OK si tuvo éxito.
 */
esp_err_t audio_tx_start(void);

/**
 * @brief Finaliza la transmisión (Detiene DAC, Reinicia ADC).
 * @return esp_err_t ESP_OK si tuvo éxito.
 */
esp_err_t audio_tx_stop(void);

#endif // AUDIO_DRIVER_H
