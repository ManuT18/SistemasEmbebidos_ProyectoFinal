#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "audio_driver.h"

// Includes de FT8
#include "kiss_fftr.h"
#include "ft8/decode.h"
#include "ft8/constants.h"

#define FT8_TASK_STACK_SIZE 8192 // Aumentamos stack por si acaso
#define FT8_TASK_PRIORITY   5

// Configuración FT8
#define FT8_SAMPLE_RATE 12000
#define FFT_SIZE        1920 // 12000 Hz * 0.160 s = 1920 muestras por símbolo
#define NUM_BINS        (FFT_SIZE / 2 + 1)

// Variables globales para FFT y Waterfall
// Las definimos estáticas para no saturar el stack de la tarea
static kiss_fftr_cfg fft_cfg;
static float window[FFT_SIZE];

void ft8_task(void *pvParameters)
{
    ESP_LOGI("FT8_TASK", "Tarea FT8 iniciada");

    // 1. Inicializar FFT
    fft_cfg = kiss_fftr_alloc(FFT_SIZE, 0, NULL, NULL);
    if (fft_cfg == NULL) {
        ESP_LOGE("FT8_TASK", "Error al asignar memoria para FFT");
        vTaskDelete(NULL);
    }

    // 2. Pre-calcular ventana de Hanning
    for (int i = 0; i < FFT_SIZE; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }

    // 3. Inicializar Waterfall
    // FT8 tiene 79 símbolos por mensaje (12.64s), pero el slot es de 15s.
    // Capturamos un poco más para tener margen. Digamos 14s = ~87 bloques.
    const int num_blocks = 87; 
    waterfall_t power = {
        .num_blocks = num_blocks,
        .num_bins = NUM_BINS,
        .time_osr = 1,
        .freq_osr = 1,
        .mag = NULL
    };
    power.mag = malloc(num_blocks * NUM_BINS * sizeof(uint8_t));
    power.protocol = PROTO_FT8; 

    if (power.mag == NULL) {
        ESP_LOGE("FT8_TASK", "Error al asignar memoria para Waterfall");
        free(fft_cfg);
        vTaskDelete(NULL);
    }

    // Buffers temporales
    int16_t *audio_buf = malloc(FFT_SIZE * sizeof(int16_t));
    kiss_fft_scalar *fft_in = malloc(FFT_SIZE * sizeof(kiss_fft_scalar));
    kiss_fft_cpx *fft_out = malloc(NUM_BINS * sizeof(kiss_fft_cpx));

    if (!audio_buf || !fft_in || !fft_out) {
        ESP_LOGE("FT8_TASK", "Error de memoria en buffers temporales");
        // Liberar lo que se pudo
        if(audio_buf) free(audio_buf);
        if(fft_in) free(fft_in);
        if(fft_out) free(fft_out);
        free(power.mag);
        free(fft_cfg);
        vTaskDelete(NULL);
    }

    size_t bytes_read = 0;

    while (1) {
        ESP_LOGI("FT8_TASK", "Esperando inicio de ciclo (simulado)...");
        // TODO: Sincronizar con PPS/RTC. Por ahora arrancamos "ya".
        
        // Limpiar waterfall
        memset(power.mag, 0, num_blocks * NUM_BINS);

        // --- FASE 1: CAPTURA Y FFT ---
        ESP_LOGI("FT8_TASK", "Capturando audio...");
        for (int i = 0; i < num_blocks; i++) {
            // Leer bloque de audio (1920 muestras)
            // audio_read devuelve bytes leídos. 
            // Queremos leer FFT_SIZE muestras.
            esp_err_t ret = audio_read(audio_buf, FFT_SIZE, &bytes_read);
            
            if (ret != ESP_OK) {
                ESP_LOGW("FT8_TASK", "Error leyendo audio");
                continue;
            }

            // Aplicar ventana y convertir a float para FFT
            for (int j = 0; j < FFT_SIZE; j++) {
                fft_in[j] = ((float)audio_buf[j]) * window[j];
            }

            // Ejecutar FFT
            kiss_fftr(fft_cfg, fft_in, fft_out);

            // Calcular magnitud (dB) y guardar en waterfall
            for (int j = 0; j < NUM_BINS; j++) {
                float mag = sqrtf(fft_out[j].r * fft_out[j].r + fft_out[j].i * fft_out[j].i);
                // Convertir a dB (aprox) y escalar a uint8_t
                // Ajustar ganancia según micrófono. Valor empírico.
                float db = 10.0f * log10f(mag + 1e-9f); 
                int val = (int)(db * 2.0f); // Escalar
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                
                power.mag[i * NUM_BINS + j] = (uint8_t)val;
            }
        }

        // --- FASE 2: DECODIFICACIÓN ---
        ESP_LOGI("FT8_TASK", "Decodificando...");
        
        // Buscar candidatos (sincronización)
        const int max_candidates = 10;
        candidate_t heap[10];
        int num_candidates = ft8_find_sync(&power, max_candidates, heap, 0); // min_score = 0

        ESP_LOGI("FT8_TASK", "Candidatos encontrados: %d", num_candidates);

        // Intentar decodificar cada candidato
        for (int i = 0; i < num_candidates; i++) {
            message_t msg;
            decode_status_t status;
            
            if (ft8_decode(&power, &heap[i], &msg, 20, &status)) {
                ESP_LOGI("FT8_DECODE", "MENSAJE: %s | SNR: %d | DT: %.2f", 
                         msg.text, 
                         heap[i].score, 
                         (float)heap[i].time_offset * FT8_SYMBOL_PERIOD);
            }
        }

        // Esperar un poco antes del siguiente ciclo (simulando fin de slot)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

    // 1. Inicializar Hardware de Audio
    if (audio_driver_init() != ESP_OK) {
        ESP_LOGE("MAP-FT8", "Fallo al inicializar audio. Abortando.");
        return;
    }

    // 2. Crear Tarea Principal de Procesamiento
    xTaskCreate(ft8_task, "ft8_task", FT8_TASK_STACK_SIZE, NULL, FT8_TASK_PRIORITY, NULL);
}