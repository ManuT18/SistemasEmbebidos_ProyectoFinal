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
#include "driver/dac_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "AUDIO_DRIVER";

// Configuración ADC (RX) - OneShot
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_6 // GPIO 34
#define ADC_ATTEN           ADC_ATTEN_DB_12

// Configuración DAC (TX) - Continuous
#define DAC_CHAN            DAC_CHAN_0 // GPIO 25
#define AUDIO_SAMPLE_RATE   12000
#define HW_DAC_SAMPLE_RATE  24000 // Upsampling 2x para DAC

// Handles
static adc_oneshot_unit_handle_t adc_handle = NULL;
static dac_continuous_handle_t dac_handle = NULL;
static SemaphoreHandle_t audio_mutex = NULL;
static SemaphoreHandle_t rx_running_sem = NULL; // Mutex para pausar RX
static bool dac_running = false;

// Ring Buffer para desacoplar captura de procesamiento
static RingbufHandle_t adc_ringbuf = NULL;
static TaskHandle_t capture_task_handle = NULL;
static volatile bool capture_running = false;

// Buffer interno para la tarea de captura
#define CAPTURE_BUFFER_SIZE 128 // Pequeño buffer para agrupar escrituras en RingBuf

static esp_err_t create_adc_handle(void) {
    if (adc_handle) return ESP_OK;

    ESP_LOGI(TAG, "Creando handle ADC OneShot...");
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &adc_handle), TAG, "Error init ADC unit");

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config), TAG, "Error config ADC channel");

    return ESP_OK;
}

static esp_err_t create_dac_handle(void) {
    if (dac_handle) return ESP_OK;

    ESP_LOGI(TAG, "Creando handle DAC Continuous...");
    dac_continuous_config_t dac_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,
        .desc_num = 4,
        .buf_size = 2048, 
        .freq_hz = HW_DAC_SAMPLE_RATE, 
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    
    ESP_RETURN_ON_ERROR(dac_continuous_new_channels(&dac_cfg, &dac_handle), TAG, "Error creando handle DAC");
    return ESP_OK;
}

#include "esp_task_wdt.h"

// ... (existing includes)

// ... (existing code)

// Tarea de Captura de Audio (Alta Prioridad)
static void adc_capture_task(void *arg) {
    ESP_LOGI(TAG, "Tarea de Captura ADC iniciada");
    
    // Registrar tarea en WDT
    esp_task_wdt_add(NULL);

    const double period_us = 83.333333; // 12000 Hz
    double next_time = (double)esp_timer_get_time();
    
    int16_t sample_batch[CAPTURE_BUFFER_SIZE];
    int batch_idx = 0;

    // Debug VU Meter
    int min_val = 4096;
    int max_val = 0;
    int samples_counted = 0;

    ESP_LOGI(TAG, " AUDIO DRIVER ################# Tarea de Captura ADC: Entrando al bucle infinito");
    
    int yield_counter = 0;

    while (1) {
        // Reset WDT de esta tarea
        esp_task_wdt_reset();

        if (!capture_running) {
            vTaskDelay(pdMS_TO_TICKS(100));
            next_time = (double)esp_timer_get_time(); 
            continue;
        }

        // Espera precisa
        while (esp_timer_get_time() < (int64_t)next_time) {
            __asm__ __volatile__("nop");
        }

        // Leer ADC
        int val = 0;
        if (adc_handle) {
            if (adc_oneshot_read(adc_handle, ADC_CHANNEL, &val) == ESP_OK) {
                sample_batch[batch_idx++] = (int16_t)val;
                
                // VU Meter Logic
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            } else {
                sample_batch[batch_idx++] = 2048;
            }
        }

        next_time += period_us;
        samples_counted++;
        yield_counter++;

        // Enviar al Ring Buffer
        if (batch_idx >= CAPTURE_BUFFER_SIZE) {
            if (adc_ringbuf) {
                xRingbufferSend(adc_ringbuf, sample_batch, sizeof(sample_batch), 0); 
            }
            batch_idx = 0;
            taskYIELD();
        }
        
        // Log cada ~1 segundo
        if (samples_counted >= 12000) {
            printf("ADC Signal: Min=%d, Max=%d, Delta=%d (Center ~2048)\n", min_val, max_val, max_val - min_val);
            samples_counted = 0;
            min_val = 4096;
            max_val = 0;
        }

        // CRÍTICO: Ceder control al IDLE task cada ~2.5 segundos para evitar WDT Reset
        // 30000 muestras * 83us = ~2.5s
        if (yield_counter >= 30000) {
            vTaskDelay(1); // Esperar 1 tick (10ms)
            yield_counter = 0;
            // Compensar el tiempo perdido para no intentar "recuperar" el pasado de golpe
            next_time = (double)esp_timer_get_time(); 
        }
    }
}

void audio_pause_rx(void) {
    if (rx_running_sem) {
        xSemaphoreTake(rx_running_sem, portMAX_DELAY);
        xSemaphoreGive(rx_running_sem);
    }
}

void audio_resume_rx(void) {
    // No-op en este diseño, controlado por capture_running
}

void audio_flush_rx(void) {
    if (adc_ringbuf) {
        // Leer y descartar todo lo que haya en el buffer
        size_t item_size;
        void *item;
        while ((item = xRingbufferReceive(adc_ringbuf, &item_size, 0)) != NULL) {
            vRingbufferReturnItem(adc_ringbuf, item);
        }
    }
}

esp_err_t audio_driver_init(void)
{
    ESP_LOGI(TAG, "Inicializando Audio Driver (Hybrid + RingBuf)...");
    
    if (audio_mutex == NULL) audio_mutex = xSemaphoreCreateMutex();
    if (rx_running_sem == NULL) rx_running_sem = xSemaphoreCreateMutex();

    // Crear Ring Buffer (aprox 1.5 segundo de audio = 12000 * 2 bytes * 1.5 = 36KB)
    // Usamos 20KB para asegurar
    if (adc_ringbuf == NULL) {
        adc_ringbuf = xRingbufferCreate(20480, RINGBUF_TYPE_BYTEBUF);
        if (adc_ringbuf == NULL) {
            ESP_LOGE(TAG, "Error creando Ring Buffer");
            return ESP_FAIL;
        }
    }

    ESP_RETURN_ON_ERROR(create_adc_handle(), TAG, "Fallo al crear ADC handle");
    ESP_RETURN_ON_ERROR(create_dac_handle(), TAG, "Fallo al crear DAC handle");

    // Crear Tarea de Captura (Pinned to Core 1, Priority High)
    if (capture_task_handle == NULL) {
        xTaskCreatePinnedToCore(adc_capture_task, "adc_capture", 4096, NULL, configMAX_PRIORITIES - 1, &capture_task_handle, 1);
    }
    
    capture_running = true; // Iniciar captura

    return ESP_OK;
}

esp_err_t audio_read(int16_t *buffer, size_t length, size_t *bytes_read)
{
    if (!adc_ringbuf) return ESP_FAIL;

    size_t bytes_needed = length * sizeof(int16_t);
    size_t total_received = 0;
    
    // Loop until we get ALL needed bytes
    while (total_received < bytes_needed) {
        size_t chunk_size = 0;
        // Wait up to 100ms for a chunk
        void *data = xRingbufferReceiveUpTo(adc_ringbuf, &chunk_size, pdMS_TO_TICKS(100), bytes_needed - total_received);
        
        if (data) {
            memcpy((uint8_t*)buffer + total_received, data, chunk_size);
            vRingbufferReturnItem(adc_ringbuf, data);
            total_received += chunk_size;
        } else {
            // Timeout (buffer empty). 
            // Check if we should abort? For now, keep waiting.
            // But if we wait too long (e.g. 5 seconds), abort to avoid hanging forever.
            static int timeout_count = 0;
            timeout_count++;
            if (timeout_count > 50) { // 5 seconds
                ESP_LOGE(TAG, "Audio Read Timeout (Starvation)");
                if (bytes_read) *bytes_read = total_received;
                return ESP_ERR_TIMEOUT;
            }
        }
    }
    
    if (bytes_read) *bytes_read = total_received;
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
    
    // Pausar captura RX
    capture_running = false;
    audio_pause_rx(); 

    if (audio_mutex) xSemaphoreTake(audio_mutex, portMAX_DELAY);

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
    
    // Reanudar captura RX
    // Limpiar buffer viejo?
    if (adc_ringbuf) {
        // Opcional: xRingbufferClear(adc_ringbuf); // No existe API directa standard, pero podemos leer hasta vaciar
    }
    capture_running = true;
    
    return ESP_OK;
}
