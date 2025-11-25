#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "audio_driver.h"
#include "kiss_fftr.h"
#include "ft8/decode.h"
#include "ft8/constants.h"

/* --- Configuración de Tareas y FT8 --- */
#define FT8_TASK_STACK_SIZE 8192
#define FT8_TASK_PRIORITY   5
#define FT8_SAMPLE_RATE     12000
#define FFT_SIZE            1920 
#define NUM_BINS            (FFT_SIZE / 2 + 1)

/* --- Configuración de Modo Prueba (WAV Embebido) --- 
 * Descomentar para probar con archivos WAV embebidos en el binario */
#define TEST_MODE_WAV 

#ifdef TEST_MODE_WAV
/* Definición de archivos WAV embebidos en el binario */
extern const uint8_t _binary_191111_110130_wav_start[];
extern const uint8_t _binary_191111_110130_wav_end[];
extern const uint8_t _binary_191111_110145_wav_start[];
extern const uint8_t _binary_191111_110145_wav_end[];
extern const uint8_t _binary_191111_110200_wav_start[];
extern const uint8_t _binary_191111_110200_wav_end[];
extern const uint8_t _binary_191111_110215_wav_start[];
extern const uint8_t _binary_191111_110215_wav_end[];

typedef struct {
    const char* name;
    const uint8_t* start;
    const uint8_t* end;
} test_file_t;

static const test_file_t test_files[] = {
    { "191111_110130.wav", _binary_191111_110130_wav_start, _binary_191111_110130_wav_end },
    { "191111_110145.wav", _binary_191111_110145_wav_start, _binary_191111_110145_wav_end },
    { "191111_110200.wav", _binary_191111_110200_wav_start, _binary_191111_110200_wav_end },
    { "191111_110215.wav", _binary_191111_110215_wav_start, _binary_191111_110215_wav_end }
};
static const int num_test_files = sizeof(test_files) / sizeof(test_files[0]);
#endif

/* --- Variables Globales de Procesamiento (FFT) --- */
static kiss_fftr_cfg fft_cfg;
static float window[FFT_SIZE];

/* 
 * Tarea Principal FT8:
 * 1. Inicializa estructuras FFT y Waterfall.
 * 2. Bucle infinito: Captura audio (15s) -> Procesa FFT -> Decodifica mensajes.
 */
void ft8_task(void *pvParameters)
{
    ESP_LOGI("FT8_TASK", "Tarea FT8 iniciada");

    /* Inicialización de recursos para DSP */
    fft_cfg = kiss_fftr_alloc(FFT_SIZE, 0, NULL, NULL);
    if (fft_cfg == NULL) {
        ESP_LOGE("FT8_TASK", "Fallo alloc FFT");
        vTaskDelete(NULL);
    }

    /* Pre-cálculo de Ventana Hanning para suavizado de FFT */
    for (int i = 0; i < FFT_SIZE; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }

    /* Configuración del Waterfall (Espectrograma) */
    const int num_blocks = 87; // Bloques de tiempo por slot
    waterfall_t power = {
        .num_blocks = num_blocks,
        .num_bins = NUM_BINS,
        .time_osr = 1,
        .freq_osr = 1,
        .mag = NULL
    };
    power.mag = malloc(num_blocks * NUM_BINS * sizeof(uint8_t));
    power.protocol = PROTO_FT8; 

    /* Buffers de Audio y FFT */
    int16_t *audio_buf = malloc(FFT_SIZE * sizeof(int16_t));
    kiss_fft_scalar *fft_in = malloc(FFT_SIZE * sizeof(kiss_fft_scalar));
    kiss_fft_cpx *fft_out = malloc(NUM_BINS * sizeof(kiss_fft_cpx));

    if (!power.mag || !audio_buf || !fft_in || !fft_out) {
        ESP_LOGE("FT8_TASK", "Fallo alloc buffers");
        // Liberación segura omitida por brevedad en error fatal
        vTaskDelete(NULL);
    }

    size_t __attribute__((unused)) bytes_read = 0;
    int current_file_idx = 0;

#ifdef TEST_MODE_WAV
    /* Configuración inicial para lectura de primer archivo WAV */
    const uint8_t *wav_start = test_files[current_file_idx].start;
    const uint8_t *wav_end = test_files[current_file_idx].end;
    size_t wav_pos = 44; // Saltar header WAV (44 bytes)
    size_t wav_size = wav_end - wav_start;
    
    uint32_t wav_sample_rate = *((uint32_t*)(wav_start + 24));
    printf("\n--- Procesando: %s (SR: %lu Hz) ---\n", test_files[current_file_idx].name, wav_sample_rate);
#endif

    while (1) {
        /* Limpiar waterfall para nuevo ciclo */
        memset(power.mag, 0, num_blocks * NUM_BINS);

        /* --- FASE 1: Captura y Procesamiento FFT (Tiempo Real Simulado) --- */
        for (int i = 0; i < num_blocks; i++) {
            
#ifdef TEST_MODE_WAV
            /* Lectura desde Memoria Flash (Modo Test) */
            size_t bytes_to_read = FFT_SIZE * sizeof(int16_t);
            
            if (wav_pos + bytes_to_read > wav_size) {
                memset(audio_buf, 0, bytes_to_read); // Rellenar silencio si acaba archivo
            } else {
                memcpy(audio_buf, wav_start + wav_pos, bytes_to_read);
                wav_pos += bytes_to_read;
            }
            vTaskDelay(pdMS_TO_TICKS(1)); // Delay mínimo para evitar bloqueo CPU
#else
            /* Lectura desde ADC (Hardware Real) */
            if (audio_read(audio_buf, FFT_SIZE, &bytes_read) != ESP_OK) {
                continue;
            }
#endif

            /* Aplicar Ventana y FFT */
            for (int j = 0; j < FFT_SIZE; j++) {
                fft_in[j] = ((float)audio_buf[j]) * window[j];
            }
            kiss_fftr(fft_cfg, fft_in, fft_out);

            /* Calcular Magnitud (dB) y guardar en Waterfall */
            for (int j = 0; j < NUM_BINS; j++) {
                float mag = sqrtf(fft_out[j].r * fft_out[j].r + fft_out[j].i * fft_out[j].i);
                float db = 10.0f * log10f(mag + 1e-9f); 
                int val = (int)(db * 2.0f); // Escalar para uint8
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                power.mag[i * NUM_BINS + j] = (uint8_t)val;
            }
        }

        /* --- FASE 2: Decodificación FT8 --- */
        const int max_candidates = 10;
        candidate_t heap[10];
        int num_candidates = ft8_find_sync(&power, max_candidates, heap, 0);

        printf("INFORMACION DECODIFICADA (Candidatos: %d)\n", num_candidates);

        for (int i = 0; i < num_candidates; i++) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevenir Watchdog

            message_t msg;
            decode_status_t status;
            
            if (ft8_decode(&power, &heap[i], &msg, 20, &status)) {
                printf("MENSAJE: %s | SNR: %d | DT: %.2f\n", 
                         msg.text, heap[i].score, (float)heap[i].time_offset * FT8_SYMBOL_PERIOD);
            } else {
                printf("FALLO: SNR=%d | LDPC=%d CRC=0x%04x/0x%04x\n",
                        heap[i].score, status.ldpc_errors, status.crc_extracted, status.crc_calculated);
            }
        }
        printf("\n");

#ifdef TEST_MODE_WAV
        /* Lógica de cambio de archivo para Test */
        current_file_idx++;
        if (current_file_idx < num_test_files) {
            wav_start = test_files[current_file_idx].start;
            wav_end = test_files[current_file_idx].end;
            wav_pos = 44;
            wav_size = wav_end - wav_start;
            
            uint32_t next_sr = *((uint32_t*)(wav_start + 24));
            printf("--- Procesando: %s (SR: %lu Hz) ---\n", test_files[current_file_idx].name, next_sr);
        } else {
            printf("TEST FINALIZADO\n");
            vTaskSuspend(NULL);
        }
#else
        vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar siguiente slot en modo real
#endif
    }
}

/* 
 * Punto de Entrada de la Aplicación:
 * Inicializa hardware (si no es test) y lanza la tarea de procesamiento.
 */
void app_main(void)
{
    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

#ifndef TEST_MODE_WAV
    if (audio_driver_init() != ESP_OK) {
        ESP_LOGE("MAP-FT8", "Fallo init audio");
        return;
    }
#else
    ESP_LOGW("MAP-FT8", "Modo Test: Audio Hardware desactivado");
#endif

    xTaskCreate(ft8_task, "ft8_task", FT8_TASK_STACK_SIZE, NULL, FT8_TASK_PRIORITY, NULL);
}