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

#define FT8_TASK_STACK_SIZE 8192
#define FT8_TASK_PRIORITY   5

// Descomentar para activar modo prueba con archivo WAV embebido
#define TEST_MODE_WAV 

#ifdef TEST_MODE_WAV
// Símbolos generados por el linker para los archivos embebidos
// Nota: CMake reemplaza puntos y barras con guiones bajos
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

// Configuración FT8
#define FT8_SAMPLE_RATE 12000
#define FFT_SIZE        1920 // 12000 Hz * 0.160 s = 1920 muestras por símbolo
#define NUM_BINS        (FFT_SIZE / 2 + 1)

// Variables globales para FFT y Waterfall
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
        if(audio_buf) free(audio_buf);
        if(fft_in) free(fft_in);
        if(fft_out) free(fft_out);
        free(power.mag);
        free(fft_cfg);
        vTaskDelete(NULL);
    }

    size_t __attribute__((unused)) bytes_read = 0;
    int current_file_idx = 0;

#ifdef TEST_MODE_WAV
    ESP_LOGW("FT8_TASK", "MODO TEST ACTIVADO: Leyendo audio desde memoria flash");
    
    // Variables de estado para el archivo actual
    const uint8_t *wav_start = test_files[current_file_idx].start;
    const uint8_t *wav_end = test_files[current_file_idx].end;
    size_t wav_pos = 44; // Saltar header WAV típico
    size_t wav_size = wav_end - wav_start;
    
    printf("\n--- Procesando archivo: %s ---\n", test_files[current_file_idx].name);
#endif

    while (1) {
        // Limpiar waterfall
        memset(power.mag, 0, num_blocks * NUM_BINS);

        // --- FASE 1: CAPTURA Y FFT ---
        // ESP_LOGI("FT8_TASK", "Capturando audio..."); // Comentado para limpiar salida en test
        for (int i = 0; i < num_blocks; i++) {
            
#ifdef TEST_MODE_WAV
            size_t bytes_to_read = FFT_SIZE * sizeof(int16_t);
            
            // Verificar si llegamos al final del archivo actual
            if (wav_pos + bytes_to_read > wav_size) {
                // Rellenar con ceros si falta un poco al final
                memset(audio_buf, 0, bytes_to_read);
            } else {
                memcpy(audio_buf, wav_start + wav_pos, bytes_to_read);
                wav_pos += bytes_to_read;
            }
            
            // Simular tiempo de captura (muy acelerado para pruebas masivas)
            vTaskDelay(pdMS_TO_TICKS(1)); 
#else
            // Leer del ADC
            esp_err_t ret = audio_read(audio_buf, FFT_SIZE, &bytes_read);
            if (ret != ESP_OK) {
                ESP_LOGW("FT8_TASK", "Error leyendo audio");
                continue;
            }
#endif

            // Aplicar ventana y convertir a float para FFT
            for (int j = 0; j < FFT_SIZE; j++) {
                fft_in[j] = ((float)audio_buf[j]) * window[j];
            }

            // Ejecutar FFT
            kiss_fftr(fft_cfg, fft_in, fft_out);

            // Calcular magnitud (dB) y guardar en waterfall
            for (int j = 0; j < NUM_BINS; j++) {
                float mag = sqrtf(fft_out[j].r * fft_out[j].r + fft_out[j].i * fft_out[j].i);
                float db = 10.0f * log10f(mag + 1e-9f); 
                int val = (int)(db * 2.0f); 
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                
                power.mag[i * NUM_BINS + j] = (uint8_t)val;
            }
        }

        // --- FASE 2: DECODIFICACIÓN ---
        // ESP_LOGI("FT8_TASK", "Decodificando...");
        
        const int max_candidates = 10;
        candidate_t heap[10];
        int num_candidates = ft8_find_sync(&power, max_candidates, heap, 0);

        // Salida formateada solicitada
        printf("INFORMACION DECODIFICADA\n");

        for (int i = 0; i < num_candidates; i++) {
            message_t msg;
            decode_status_t status;
            
            if (ft8_decode(&power, &heap[i], &msg, 20, &status)) {
                printf("MENSAJE: %s | SNR: %d | DT: %.2f\n", 
                         msg.text, 
                         heap[i].score, 
                         (float)heap[i].time_offset * FT8_SYMBOL_PERIOD);
            }
        }
        printf("\n"); // Separador

#ifdef TEST_MODE_WAV
        // Pasar al siguiente archivo
        current_file_idx++;
        if (current_file_idx < num_test_files) {
            wav_start = test_files[current_file_idx].start;
            wav_end = test_files[current_file_idx].end;
            wav_pos = 44;
            wav_size = wav_end - wav_start;
            printf("--- Procesando archivo: %s ---\n", test_files[current_file_idx].name);
        } else {
            printf("TEST FINALIZADO\n");
            vTaskSuspend(NULL); // Detener la tarea
        }
#else
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }
}

void app_main(void)
{
    ESP_LOGI("MAP-FT8", "Iniciando aplicación MAP-FT8...");

    // 1. Inicializar Hardware de Audio
#ifndef TEST_MODE_WAV
    if (audio_driver_init() != ESP_OK) {
        ESP_LOGE("MAP-FT8", "Fallo al inicializar audio. Abortando.");
        return;
    }
#else
    ESP_LOGW("MAP-FT8", "Modo Test: Saltando inicialización de hardware de audio");
#endif

    // 2. Crear Tarea Principal de Procesamiento
    xTaskCreate(ft8_task, "ft8_task", FT8_TASK_STACK_SIZE, NULL, FT8_TASK_PRIORITY, NULL);
}