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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

// Mutex para proteger el acceso a los handles y evitar condiciones de carrera
static SemaphoreHandle_t audio_mutex = NULL;
static bool adc_running = false;
static bool dac_running = false;

// Sincronización RX
static volatile bool rx_paused = false;
static SemaphoreHandle_t rx_running_sem = NULL;

// --- Funciones Helper Privadas ---

static esp_err_t create_adc_handle(void) {
    if (adc_handle) return ESP_OK;

    ESP_LOGI(TAG, "Creando handle ADC...");
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = AUDIO_BUFFER_SIZE * 8,
        .conv_frame_size = AUDIO_BUFFER_SIZE * 2,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&adc_config, &adc_handle), TAG, "Error creando handle ADC");

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = HW_SAMPLE_RATE,
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
    return ESP_OK;
}

static esp_err_t create_dac_handle(void) {
    if (dac_handle) return ESP_OK;

    ESP_LOGI(TAG, "Creando handle DAC...");
    dac_continuous_config_t dac_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,
        .desc_num = 4,
        .buf_size = AUDIO_BUFFER_SIZE * 2,
        .freq_hz = HW_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    
    ESP_RETURN_ON_ERROR(dac_continuous_new_channels(&dac_cfg, &dac_handle), TAG, "Error creando handle DAC");
    return ESP_OK;
}

// --- Implementación Pública ---

void audio_pause_rx(void) {
    rx_paused = true;
    // Esperar a que termine cualquier lectura en curso usando el semáforo
    if (rx_running_sem) {
        xSemaphoreTake(rx_running_sem, portMAX_DELAY);
        xSemaphoreGive(rx_running_sem);
    }
}

void audio_resume_rx(void) {
    rx_paused = false;
}

esp_err_t audio_driver_init(void)
{
    ESP_LOGI(TAG, "Inicializando Audio Driver (Handles Persistentes)...");
    
    if (audio_mutex == NULL) {
        audio_mutex = xSemaphoreCreateMutex();
    }
    if (rx_running_sem == NULL) {
        rx_running_sem = xSemaphoreCreateMutex();
    }

    // Crear ambos handles al inicio
    ESP_RETURN_ON_ERROR(create_adc_handle(), TAG, "Fallo al crear ADC handle");
    
    // Intentar crear DAC handle. Si falla por conflicto, lo logueamos pero seguimos (se intentará en TX)
    esp_err_t ret = create_dac_handle();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo crear DAC handle al inicio (posible conflicto HW): %d", ret);
        // No retornamos error fatal, quizás funcione alternándolos
    }

    // Iniciar ADC (Modo RX por defecto)
    ESP_LOGI(TAG, "Iniciando ADC...");
    ESP_RETURN_ON_ERROR(adc_continuous_start(adc_handle), TAG, "Error iniciando ADC");
    adc_running = true;

    return ESP_OK;
}

esp_err_t audio_write(const int16_t *buffer, size_t length, size_t *bytes_written)
{
    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

    if (!dac_handle) {
        ESP_LOGE(TAG, "DAC Handle es NULL (TX no iniciada)");
        if (audio_mutex) xSemaphoreGive(audio_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t samples_processed = 0;
    // Buffer temporal para escritura HW
    #define CHUNK_SIZE 256
    uint8_t tmp_buf[CHUNK_SIZE * 2]; 
    
    while (samples_processed < length) {
        size_t remaining = length - samples_processed;
        size_t chunk_samples = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        
        // Upsampling (Interpolación 1:2)
        for (size_t i = 0; i < chunk_samples; i++) {
            int32_t val = buffer[samples_processed + i];
            val = (val + 32768) >> 8; 
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            
            tmp_buf[i*2] = (uint8_t)val;
            tmp_buf[i*2 + 1] = (uint8_t)val;
        }
        
        size_t written_bytes = 0;
        esp_err_t ret = dac_continuous_write(dac_handle, tmp_buf, chunk_samples * 2, &written_bytes, 1000);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error escribiendo al DAC: %d", ret);
            if (audio_mutex) xSemaphoreGive(audio_mutex);
            return ret;
        }
        
        samples_processed += chunk_samples;
        
        // Yield para evitar watchdog
        vTaskDelay(1);
    }
    
    if (audio_mutex) xSemaphoreGive(audio_mutex);

    if (bytes_written) {
        *bytes_written = samples_processed * sizeof(int16_t);
    }
    
    return ESP_OK;
}

esp_err_t audio_tx_start(void)
{
    ESP_LOGI(TAG, ">>> Cambiando a MODO TX (Stop ADC -> Start DAC) <<<");
    
    audio_pause_rx(); // Sincronización con semáforo

    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

    // 1. Detener ADC
    if (adc_running && adc_handle) {
        adc_continuous_stop(adc_handle);
        adc_running = false;
    }

    // Pequeño delay para que el HW cambie de estado
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Asegurar DAC handle
    if (!dac_handle) {
        create_dac_handle();
    }

    // 3. Habilitar DAC
    if (dac_handle) {
        esp_err_t ret = dac_continuous_enable(dac_handle);
        if (ret == ESP_OK) {
            dac_running = true;
        } else {
            ESP_LOGE(TAG, "Error habilitando DAC: %d", ret);
            if (audio_mutex) xSemaphoreGive(audio_mutex);
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "No hay DAC handle disponible");
        if (audio_mutex) xSemaphoreGive(audio_mutex);
        return ESP_FAIL;
    }
    
    if (audio_mutex) xSemaphoreGive(audio_mutex);
    return ESP_OK;
}

esp_err_t audio_tx_stop(void)
{
    ESP_LOGI(TAG, ">>> Cambiando a MODO RX (Stop DAC -> Start ADC) <<<");
    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

    // 1. Deshabilitar DAC
    if (dac_running && dac_handle) {
        dac_continuous_disable(dac_handle);
        dac_running = false;
    }

    // Pequeño delay
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Iniciar ADC
    if (adc_handle) {
        esp_err_t ret = adc_continuous_start(adc_handle);
        if (ret == ESP_OK) {
            adc_running = true;
        } else {
            ESP_LOGE(TAG, "Error iniciando ADC: %d", ret);
            if (audio_mutex) xSemaphoreGive(audio_mutex);
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "No hay ADC handle disponible");
        if (audio_mutex) xSemaphoreGive(audio_mutex);
        return ESP_FAIL;
    }
    
    if (audio_mutex) xSemaphoreGive(audio_mutex);
    
    audio_resume_rx();
    
    return ESP_OK;
}
