/**
 * @file audio_driver.c
 * @brief Implementación del Driver de Audio usando I2S.
 * 
 * Utiliza el periférico I2S del ESP32 en modo ADC/DAC built-in.
 * Nota: El ADC built-in tiene limitaciones de linealidad y ruido, pero es suficiente para pruebas básicas.
 */

#include "audio_driver.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_continuous.h"
#include "driver/dac_continuous.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "AUDIO_DRIVER";

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

// --- Implementación ---

/**
 * @brief Inicializa el driver I2S.
 * 
 * Configura:
 * - Modo: Master, RX/TX, ADC/DAC Built-in.
 * - Frecuencia de muestreo: 12000 Hz.
 * - Formato: 16-bit PCM.
 * 
 * @return ESP_OK si todo fue correcto.
 */
esp_err_t audio_driver_init(void)
{
    ESP_LOGI(TAG, "Inicializando Audio Driver (ADC/DAC Internos)...");
    // esp_err_t ret;

    // 1. Inicializar ADC Continuous (RX)
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = AUDIO_BUFFER_SIZE * 4,
        .conv_frame_size = AUDIO_BUFFER_SIZE,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&adc_config, &adc_handle), TAG, "Error creando handle ADC");

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = AUDIO_SAMPLE_RATE,
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

    // 2. Inicializar DAC Continuous (TX)
    dac_continuous_config_t dac_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0, // GPIO25
        .desc_num = 4,
        .buf_size = AUDIO_BUFFER_SIZE,
        .freq_hz = AUDIO_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT, // APB
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    
    ESP_RETURN_ON_ERROR(dac_continuous_new_channels(&dac_cfg, &dac_handle), TAG, "Error creando handle DAC");
    ESP_RETURN_ON_ERROR(dac_continuous_enable(dac_handle), TAG, "Error habilitando DAC");

    ESP_LOGI(TAG, "Audio Driver inicializado correctamente.");
    return ESP_OK;
}

esp_err_t audio_read(int16_t *buffer, size_t length, size_t *bytes_read)
{
    if (!adc_handle) return ESP_ERR_INVALID_STATE;
    
    uint32_t ret_num = 0;
    // ADC Continuous devuelve datos crudos (adc_digi_output_data_t). 
    // Para simplificar, leemos en un buffer temporal y convertimos si es necesario.
    // Aquí asumimos que el usuario maneja el formato raw o ajustamos.
    // Dado que length es en muestras (int16), leemos bytes = length * 2.
    
    esp_err_t ret = adc_continuous_read(adc_handle, (uint8_t*)buffer, length * sizeof(int16_t), &ret_num, 100);
    
    if (ret == ESP_OK) {
        *bytes_read = ret_num;
    }
    return ret;
}

esp_err_t audio_write(const int16_t *buffer, size_t length, size_t *bytes_written)
{
    if (!dac_handle) return ESP_ERR_INVALID_STATE;
    
    // DAC Continuous espera uint8_t (8-bit DAC).
    // Debemos convertir int16_t a uint8_t.
    // FT8 genera audio con amplitud variable, asumimos entrada 16-bit signed.
    // Convertir: (val + 32768) >> 8
    
    // Buffer temporal para conversión (en stack si es pequeño, o malloc)
    // Para eficiencia, procesamos por chunks o asumimos que el caller ya envía 8-bit si cambiamos el header.
    // Pero el header dice int16_t. Haremos conversión al vuelo.
    
    // NOTA: Para no complicar con mallocs aquí, escribiremos en bloques pequeños o 
    // pediremos al caller que envíe formato compatible. 
    // Por ahora, implementación simple: conversión in-place si el buffer fuera modificable, 
    // pero es const. Usamos un buffer estático pequeño para chunks.
    
    uint8_t tmp_buf[AUDIO_BUFFER_SIZE]; 
    size_t chunk_size = (length > AUDIO_BUFFER_SIZE) ? AUDIO_BUFFER_SIZE : length;
    
    for (size_t i = 0; i < chunk_size; i++) {
        // Escalar 16-bit signed a 8-bit unsigned
        int32_t val = buffer[i];
        val = (val + 32768) >> 8; // Shift simple
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        tmp_buf[i] = (uint8_t)val;
    }
    
    size_t written = 0;
    esp_err_t ret = dac_continuous_write(dac_handle, tmp_buf, chunk_size, &written, 100);
    
    // Ajustamos bytes_written para reflejar la entrada (int16) consumida
    // Si escribimos N bytes (muestras de 8 bits), consumimos N muestras de 16 bits (2*N bytes).
    if (ret == ESP_OK) {
        *bytes_written = written * 2; 
    }
    
    return ret;
}
