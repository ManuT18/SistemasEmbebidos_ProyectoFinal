#include "ft8_decode_task.h"

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

/* --- Configuración DSP FT8 --- */
#define FT8_SAMPLE_RATE     12000
#define FFT_SIZE            1920 
// Limitamos a 3000 Hz para ahorrar RAM (FT8 usa ~50-3000 Hz)
#define MAX_FREQ            3000
#define NUM_BINS            (MAX_FREQ * FFT_SIZE / FT8_SAMPLE_RATE) 

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

void ft8_decode_task(void *pvParameters)
{
    ESP_LOGI("FT8_DECODE", "Tarea de Decodificación FT8 iniciada");

    /* Inicialización de recursos para DSP */
    fft_cfg = kiss_fftr_alloc(FFT_SIZE, 0, NULL, NULL);
    if (fft_cfg == NULL) {
        ESP_LOGE("FT8_DECODE", "Fallo alloc FFT");
        vTaskDelete(NULL);
    }

    /* Pre-cálculo de Ventana Hanning para suavizado de FFT */
    for (int i = 0; i < FFT_SIZE; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }

    /* Configuración del Waterfall (Espectrograma) */
    // Time OSR = 2 para mejor sincronización (Solapamiento 50%)
    const int time_osr = 2;
    const int num_blocks = 87 * time_osr; 
    
    waterfall_t power = {
        .num_blocks = num_blocks,
        .num_bins = NUM_BINS,
        .time_osr = time_osr,
        .freq_osr = 1,
        .mag = NULL
    };
    
    // Asignación dinámica de memoria para el waterfall
    power.mag = malloc(num_blocks * NUM_BINS * sizeof(uint8_t));
    power.protocol = PROTO_FT8; 

    /* Buffers de Audio y FFT */
    int16_t *audio_buf = malloc(FFT_SIZE * sizeof(int16_t)); // Buffer completo para FFT
    kiss_fft_scalar *fft_in = malloc(FFT_SIZE * sizeof(kiss_fft_scalar));
    kiss_fft_cpx *fft_out = malloc((FFT_SIZE / 2 + 1) * sizeof(kiss_fft_cpx)); // Salida completa FFT

    if (!power.mag || !audio_buf || !fft_in || !fft_out) {
        ESP_LOGE("FT8_DECODE", "Fallo alloc buffers (Heap insuficiente?)");
        vTaskDelete(NULL);
    }

    // Inicializar buffer de audio con ceros
    memset(audio_buf, 0, FFT_SIZE * sizeof(int16_t));

    size_t __attribute__((unused)) bytes_read = 0;
    int current_file_idx = 0;
    
    // Paso de avance: FFT_SIZE / time_osr
    const int step_size = FFT_SIZE / time_osr; 

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

        /* --- FASE 1: Captura y Procesamiento FFT (Sliding Window) --- */
        for (int i = 0; i < num_blocks; i++) {
            
            // 1. Desplazar datos antiguos (Shift)
            // Movemos la segunda mitad del buffer al principio
            memmove(audio_buf, audio_buf + step_size, (FFT_SIZE - step_size) * sizeof(int16_t));
            
            // 2. Leer nuevos datos para llenar el final del buffer
            int16_t *new_data_ptr = audio_buf + (FFT_SIZE - step_size);
            
#ifdef TEST_MODE_WAV
            size_t bytes_to_read = step_size * sizeof(int16_t);
            
            if (wav_pos + bytes_to_read > wav_size) {
                memset(new_data_ptr, 0, bytes_to_read); 
            } else {
                memcpy(new_data_ptr, wav_start + wav_pos, bytes_to_read);
                wav_pos += bytes_to_read;
            }
            vTaskDelay(pdMS_TO_TICKS(1)); 
#else
            if (audio_read(new_data_ptr, step_size, &bytes_read) != ESP_OK) {
                // Si falla lectura, rellenar con 0
                memset(new_data_ptr, 0, step_size * sizeof(int16_t));
            }
#endif

            // 3. Procesar FFT sobre el buffer completo (1920 muestras)
            for (int j = 0; j < FFT_SIZE; j++) {
                fft_in[j] = ((float)audio_buf[j]) * window[j];
            }
            kiss_fftr(fft_cfg, fft_in, fft_out);

            // 4. Guardar Magnitud en Waterfall (Solo bins de interés)
            for (int j = 0; j < NUM_BINS; j++) {
                float mag = sqrtf(fft_out[j].r * fft_out[j].r + fft_out[j].i * fft_out[j].i);
                float db = 20.0f * log10f(mag + 1e-9f); 
                int val = (int)(db * 2.0f); 
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
            vTaskDelay(pdMS_TO_TICKS(10)); 

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
        vTaskDelay(pdMS_TO_TICKS(1000)); 
#endif
    }
}
