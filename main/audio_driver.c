/**
 * @file audio_driver.c
 * @brief Implementación del Driver de Audio usando I2S (TX) y ADC OneShot (RX).
 * 
 * SOLUCIÓN AL CONFLICTO I2S:
 * El ESP32 comparte I2S0 para ADC y DAC continuos. Esto causa crashes al alternar.
 * Solución: Usar ADC OneShot (ADC1) para RX y DAC Continuous (I2S0) para TX.
 * Al usar unidades diferentes (ADC1 vs I2S0/ADC2), evitamos conflictos de hardware.
 */

#include "audio_driver.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/dac_continuous.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "AUDIO_DRIVER";

// Frecuencia real del Hardware DAC (2x la requerida por FT8 para oversampling)
#define HW_DAC_SAMPLE_RATE      (AUDIO_SAMPLE_RATE * 2) // 24000 Hz

// --- Configuración ADC (RX - OneShot) ---
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_6 // GPIO34 en ESP32
#define ADC_ATTEN           ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t adc_handle = NULL;

// --- Configuración DAC (TX - Continuous) ---
#define DAC_CHAN            DAC_CHAN_0 // GPIO25
static dac_continuous_handle_t dac_handle = NULL;

// Mutex para proteger el acceso
static SemaphoreHandle_t audio_mutex = NULL;
static bool dac_running = false;

// Sincronización RX
static volatile bool rx_paused = false;
static SemaphoreHandle_t rx_running_sem = NULL;

// --- Funciones Helper Privadas ---

static esp_err_t create_adc_handle(void) {
    if (adc_handle) return ESP_OK;

    ESP_LOGI(TAG, "Creando handle ADC OneShot (ADC1)...");
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &adc_handle), TAG, "Error creando ADC unit");

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config), TAG, "Error configurando canal ADC");

    return ESP_OK;
}

static esp_err_t create_dac_handle(void) {
    if (dac_handle) return ESP_OK;

    ESP_LOGI(TAG, "Creando handle DAC Continuous...");
    dac_continuous_config_t dac_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,
        .desc_num = 4,
        .buf_size = AUDIO_BUFFER_SIZE * 2,
        .freq_hz = HW_DAC_SAMPLE_RATE,
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
    ESP_LOGI(TAG, "Inicializando Audio Driver (Hybrid Mode)...");
    
    if (audio_mutex == NULL) {
        audio_mutex = xSemaphoreCreateMutex();
    }
    if (rx_running_sem == NULL) {
        rx_running_sem = xSemaphoreCreateMutex();
    }

    // Crear AMBOS handles al inicio. Ahora pueden coexistir.
    ESP_RETURN_ON_ERROR(create_adc_handle(), TAG, "Fallo al crear ADC handle");
    ESP_RETURN_ON_ERROR(create_dac_handle(), TAG, "Fallo al crear DAC handle");

    return ESP_OK;
}

esp_err_t audio_read(int16_t *buffer, size_t length, size_t *bytes_read)
{
    if (rx_paused) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (bytes_read) *bytes_read = 0;
        return ESP_OK;
    }

    if (rx_running_sem) xSemaphoreTake(rx_running_sem, portMAX_DELAY);

    if (!adc_handle) {
        if (rx_running_sem) xSemaphoreGive(rx_running_sem);
        return ESP_FAIL;
    }

    // Lectura temporizada por software para 12000 Hz
    // Periodo = 1000000 / 12000 = 83.33 us
    const int64_t period_us = 83; 
    int64_t next_time = esp_timer_get_time();
    int samples_read = 0;
    int val = 0;

    for (size_t i = 0; i < length; i++) {
        // Esperar al siguiente instante de muestreo
        while (esp_timer_get_time() < next_time) {
            // Busy wait para precisión (o yield si sobra mucho tiempo)
            // Para 83us, busy wait es mejor para evitar jitter de scheduler
        }
        
        // Leer ADC
        if (adc_oneshot_read(adc_handle, ADC_CHANNEL, &val) == ESP_OK) {
            // Convertir 12-bit (0-4095) a int16 centrado en 0?
            // FT8 lib espera int16. Si es raw, el decode task lo normaliza.
            // Aquí devolvemos raw 0-4095 en el int16.
            buffer[i] = (int16_t)val;
            samples_read++;
        } else {
            buffer[i] = 2048; // Valor medio por defecto en error
        }

        next_time += period_us;
    }

    if (rx_running_sem) xSemaphoreGive(rx_running_sem);
    
    if (bytes_read) *bytes_read = samples_read * sizeof(int16_t);
    return ESP_OK;
}

esp_err_t audio_write(const int16_t *buffer, size_t length, size_t *bytes_written)
{
    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

    if (!dac_handle) {
        ESP_LOGE(TAG, "DAC Handle es NULL");
        if (audio_mutex) xSemaphoreGive(audio_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t samples_processed = 0;
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
    ESP_LOGI(TAG, ">>> Cambiando a MODO TX (Enable DAC) <<<");
    
    audio_pause_rx(); 

    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

    // Solo habilitar DAC. ADC OneShot no necesita "pararse" (solo dejamos de llamar a read)
    
    // Lazy Init: Si por alguna razón el handle no existe (fallo en init?), crearlo ahora.
    if (!dac_handle) {
        ESP_LOGW(TAG, "DAC Handle no existe en TX Start, intentando crear...");
        create_dac_handle();
    }

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
        ESP_LOGE(TAG, "No se pudo crear DAC Handle");
        if (audio_mutex) xSemaphoreGive(audio_mutex);
        return ESP_FAIL;
    }
    
    if (audio_mutex) xSemaphoreGive(audio_mutex);
    return ESP_OK;
}

esp_err_t audio_tx_stop(void)
{
    ESP_LOGI(TAG, ">>> Cambiando a MODO RX (Disable DAC) <<<");
    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

    if (dac_running && dac_handle) {
        dac_continuous_disable(dac_handle);
        dac_running = false;
    }
    
    if (audio_mutex) xSemaphoreGive(audio_mutex);
    
    audio_resume_rx();
    
    return ESP_OK;
}
