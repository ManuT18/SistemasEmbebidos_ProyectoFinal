/**
 * @file audio_driver.c
 * @brief Implementación del Driver de Audio usando I2S.
 * 
 * Utiliza el periférico I2S del ESP32 en modo ADC/DAC built-in.
 * Nota: El ADC built-in tiene limitaciones de linealidad y ruido, pero es suficiente para pruebas básicas.
 * 
 * CAMBIO IMPORTANTE:
 * El hardware (ADC/DAC DMA) del ESP32 a veces no soporta frecuencias tan bajas como 12000 Hz
 * debido a límites en los divisores de reloj.
 * Solución: Configuramos el HW a 24000 Hz y hacemos resampling (2x) por software.
 */

#include "audio_driver.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_continuous.h"
#include "driver/dac_continuous.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "AUDIO_DRIVER";

// Frecuencia real del Hardware (2x la requerida por FT8)
#define HW_SAMPLE_RATE      (AUDIO_SAMPLE_RATE * 2) // 24000 Hz

// --- Configuración ADC (RX) ---
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_6 // GPIO34 en ESP32
#define ADC_CONV_MODE       ADC_CONV_SINGLE_UNIT_1
#define ADC_OUTPUT_TYPE     ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_ATTEN           ADC_ATTEN_DB_12 // 11dB o 12dB para rango completo

static adc_continuous_handle_t adc_handle = NULL;

// --- Configuración DAC (TX) ---
#define DAC_CHAN            DAC_CHAN_0 // GPIO25
static dac_continuous_handle_t dac_handle = NULL;

// --- Funciones Helper Privadas ---

static esp_err_t init_adc(void) {
    if (adc_handle) return ESP_OK; // Ya inicializado

    ESP_LOGI(TAG, "Inicializando ADC a %d Hz...", HW_SAMPLE_RATE);
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = AUDIO_BUFFER_SIZE * 8, // Buffer interno más grande
        .conv_frame_size = AUDIO_BUFFER_SIZE * 2,    // Frame size ajustado
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&adc_config, &adc_handle), TAG, "Error creando handle ADC");

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = HW_SAMPLE_RATE, // 24000 Hz
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
    };
    
    adc_digi_pattern_config_t adc_pattern[1] = {0};
    adc_pattern[0].atten = ADC_ATTEN;
    adc_pattern[0].channel = ADC_CHANNEL;
    adc_pattern[0].unit = ADC_UNIT;
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;

    ESP_RETURN_ON_ERROR(adc_continuous_config(adc_handle, &dig_cfg), TAG, "Error configurando ADC");
    ESP_RETURN_ON_ERROR(adc_continuous_start(adc_handle), TAG, "Error iniciando ADC");
    
    return ESP_OK;
}

static esp_err_t deinit_adc(void) {
    if (!adc_handle) return ESP_OK;

    ESP_LOGI(TAG, "Desinicializando ADC...");
    ESP_RETURN_ON_ERROR(adc_continuous_stop(adc_handle), TAG, "Error deteniendo ADC");
    ESP_RETURN_ON_ERROR(adc_continuous_deinit(adc_handle), TAG, "Error liberando handle ADC");
    adc_handle = NULL;
    return ESP_OK;
}

static esp_err_t init_dac(void) {
    if (dac_handle) return ESP_OK; // Ya inicializado

    ESP_LOGI(TAG, "Inicializando DAC a %d Hz...", HW_SAMPLE_RATE);
    dac_continuous_config_t dac_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0, // GPIO25
        .desc_num = 4,
        .buf_size = AUDIO_BUFFER_SIZE * 2, // Buffer HW más grande
        .freq_hz = HW_SAMPLE_RATE,         // 24000 Hz
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT, // APB
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    
    ESP_RETURN_ON_ERROR(dac_continuous_new_channels(&dac_cfg, &dac_handle), TAG, "Error creando handle DAC");
    ESP_RETURN_ON_ERROR(dac_continuous_enable(dac_handle), TAG, "Error habilitando DAC");
    
    return ESP_OK;
}

static esp_err_t deinit_dac(void) {
    if (!dac_handle) return ESP_OK;

    ESP_LOGI(TAG, "Desinicializando DAC...");
    ESP_RETURN_ON_ERROR(dac_continuous_disable(dac_handle), TAG, "Error deshabilitando DAC");
    ESP_RETURN_ON_ERROR(dac_continuous_del_channels(dac_handle), TAG, "Error liberando handle DAC");
    dac_handle = NULL;
    return ESP_OK;
}

// --- Implementación Pública ---

esp_err_t audio_driver_init(void)
{
    ESP_LOGI(TAG, "Inicializando Audio Driver (Modo RX por defecto, HW Rate: %d Hz)...", HW_SAMPLE_RATE);
    return init_adc();
}

esp_err_t audio_read(int16_t *buffer, size_t length, size_t *bytes_read)
{
    if (!adc_handle) return ESP_ERR_INVALID_STATE;
    
    // Necesitamos leer el doble de muestras del HW (24k) para obtener 'length' a 12k
    size_t samples_to_read_hw = length * 2;
    size_t bytes_to_read_hw = samples_to_read_hw * sizeof(int16_t);
    
    // Buffer temporal en stack (cuidado con el tamaño, AUDIO_BUFFER_SIZE=1024 -> 4KB stack)
    // Si AUDIO_BUFFER_SIZE crece, mover a heap.
    uint8_t raw_buf[AUDIO_BUFFER_SIZE * 2 * sizeof(int16_t)]; 
    
    uint32_t ret_num = 0;
    esp_err_t ret = adc_continuous_read(adc_handle, raw_buf, bytes_to_read_hw, &ret_num, 100);
    
    if (ret == ESP_OK) {
        // Decimación (Downsampling 2:1)
        // Tomamos 1 de cada 2 muestras
        int16_t *raw_samples = (int16_t*)raw_buf;
        size_t samples_read_hw = ret_num / sizeof(int16_t);
        size_t samples_out = 0;

        for (size_t i = 0; i < samples_read_hw; i += 2) {
            if (samples_out < length) {
                // Promedio simple para reducir aliasing (filtro básico)
                int32_t sum = raw_samples[i];
                if (i + 1 < samples_read_hw) {
                    sum += raw_samples[i+1];
                    buffer[samples_out++] = (int16_t)(sum / 2);
                } else {
                    buffer[samples_out++] = raw_samples[i];
                }
            }
        }
        
        *bytes_read = samples_out * sizeof(int16_t); // Reportamos bytes útiles (12k)
    } else if (ret == ESP_ERR_TIMEOUT) {
        // Timeout es normal si no hay suficientes datos
        *bytes_read = 0;
        return ESP_OK;
    }
    
    return ret;
}

esp_err_t audio_write(const int16_t *buffer, size_t length, size_t *bytes_written)
{
    if (!dac_handle) {
        ESP_LOGE(TAG, "DAC Handle es NULL (TX no iniciada)");
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t samples_processed = 0;
    // Buffer temporal para escritura HW (2x tamaño porque upsampleamos)
    // Procesamos en chunks más pequeños para que quepa en un buffer de stack razonable
    #define CHUNK_SIZE 256
    uint8_t tmp_buf[CHUNK_SIZE * 2]; 
    
    while (samples_processed < length) {
        size_t remaining = length - samples_processed;
        size_t chunk_samples = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        
        // Upsampling (Interpolación 1:2 - Zero Order Hold / Repetición)
        // Convertimos int16 -> uint8 (DAC) y duplicamos cada muestra
        for (size_t i = 0; i < chunk_samples; i++) {
            int32_t val = buffer[samples_processed + i];
            val = (val + 32768) >> 8; 
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            
            tmp_buf[i*2] = (uint8_t)val;
            tmp_buf[i*2 + 1] = (uint8_t)val;
        }
        
        size_t written_bytes = 0;
        // Escribimos 2x muestras al DAC
        esp_err_t ret = dac_continuous_write(dac_handle, tmp_buf, chunk_samples * 2, &written_bytes, 1000);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error escribiendo al DAC: %d", ret);
            return ret;
        }
        
        samples_processed += chunk_samples;
    }
    
    if (bytes_written) {
        *bytes_written = samples_processed * sizeof(int16_t);
    }
    
    return ESP_OK;
}

esp_err_t audio_tx_start(void)
{
    ESP_LOGI(TAG, ">>> Cambiando a MODO TX <<<");
    esp_err_t ret = deinit_adc();
    if (ret != ESP_OK) return ret;
    return init_dac();
}

esp_err_t audio_tx_stop(void)
{
    ESP_LOGI(TAG, ">>> Cambiando a MODO RX <<<");
    esp_err_t ret = deinit_dac();
    if (ret != ESP_OK) return ret;
    return init_adc();
}
