/**
 * @file ft8_decode_task.c
 * @brief Implementación de la tarea de decodificación FT8 usando monitor_t.
 * 
 * Utiliza el módulo 'monitor' de ft8_lib para el procesamiento DSP (FFT, Waterfall).
 */

#include "ft8_decode_task.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "audio_driver.h"
#include "common/monitor.h"
#include "ft8/decode.h"
#include "ft8/constants.h"
#include "ft8/message.h"
#include <sys/time.h>
#include <time.h>
#include "web_interface.h"

static const char *TAG = "FT8_DECODE";

/* --- Configuración DSP FT8 --- */
#define FT8_SAMPLE_RATE     12000

/* --- Configuración de Modo Prueba (WAV Embebido) --- 
 * Descomentar para probar con archivos WAV embebidos en el binario */
// #define TEST_MODE_WAV 

#ifdef TEST_MODE_WAV
/* Definición de archivos WAV embebidos en el binario */
extern const uint8_t _binary_websdr_test1_wav_start[];
extern const uint8_t _binary_websdr_test1_wav_end[];
extern const uint8_t _binary_websdr_test2_wav_start[];
extern const uint8_t _binary_websdr_test2_wav_end[];
extern const uint8_t _binary_websdr_test3_wav_start[];
extern const uint8_t _binary_websdr_test3_wav_end[];
extern const uint8_t _binary_websdr_test4_wav_start[];
extern const uint8_t _binary_websdr_test4_wav_end[];
extern const uint8_t _binary_websdr_test5_wav_start[];
extern const uint8_t _binary_websdr_test5_wav_end[];

typedef struct {
    const char* name;
    const uint8_t* start;
    const uint8_t* end;
} test_file_t;

static const test_file_t test_files[] = {
    { "websdr_test1.wav", _binary_websdr_test1_wav_start, _binary_websdr_test1_wav_end },
    { "websdr_test2.wav", _binary_websdr_test2_wav_start, _binary_websdr_test2_wav_end },
    { "websdr_test3.wav", _binary_websdr_test3_wav_start, _binary_websdr_test3_wav_end },
    { "websdr_test4.wav", _binary_websdr_test4_wav_start, _binary_websdr_test4_wav_end },
    { "websdr_test5.wav", _binary_websdr_test5_wav_start, _binary_websdr_test5_wav_end },
};
static const int num_test_files = sizeof(test_files) / sizeof(test_files[0]);
#endif

// Bandera global para controlar el inicio del test
static volatile bool g_run_test_flag = false;

void ft8_start_test(void) {
    g_run_test_flag = true;
    ESP_LOGI(TAG, "Solicitud de TEST recibida.");
}

void ft8_decode_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Tarea de Decodificación FT8 iniciada (Monitor Mode)");

    /* Configuración del Monitor */
    monitor_t mon;
    monitor_config_t mon_cfg = {
        .f_min = 200,
        .f_max = 3000,
        .sample_rate = FT8_SAMPLE_RATE,
        .time_osr = 1, // Reducido a 1 para ahorrar RAM (Waterfall ~40KB)
        .freq_osr = 1, // Reducido a 1 para ahorrar RAM (Waterfall ~80KB vs ~160KB)
        .protocol = FTX_PROTOCOL_FT8
    };

    monitor_init(&mon, &mon_cfg);
    ESP_LOGI(TAG, "Monitor inicializado. Block size: %d", mon.block_size);

    // Buffer temporal para conversión int16 -> float
    // monitor_process consume un bloque de mon.block_size muestras
    float *float_buf = heap_caps_malloc(mon.block_size * sizeof(float), MALLOC_CAP_8BIT);
    int16_t *pcm_buf = heap_caps_malloc(mon.block_size * sizeof(int16_t), MALLOC_CAP_8BIT);

    if (!float_buf || !pcm_buf) {
        ESP_LOGE(TAG, "Fallo alloc buffers");
        vTaskDelete(NULL);
        return;
    }

    size_t __attribute__((unused)) bytes_read = 0;

#ifdef TEST_MODE_WAV
    int current_file_idx = 0;

    /* Variables para lectura de WAV */
    const uint8_t *wav_start = NULL;
    const uint8_t *wav_end = NULL;
    size_t wav_size = 0;
    size_t wav_pos = 0;
#endif

    /* --- Máquina de Estados FT8 --- */
    typedef enum {
        FT8_STATE_IDLE,
        FT8_STATE_RX,
        FT8_STATE_DECODE,
        FT8_STATE_REPORT
    } ft8_state_t;

    ft8_state_t current_state = FT8_STATE_IDLE;
    
    while (1) {
        switch (current_state) {
            case FT8_STATE_IDLE:
#ifdef TEST_MODE_WAV
                // Esperar a que se active la bandera de test
                if (g_run_test_flag) {
                    if (current_file_idx < num_test_files) {
                        // Sincronizar con el reloj (00, 15, 30, 45)
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        struct tm *timeinfo = localtime(&tv.tv_sec);
                        int sec = timeinfo->tm_sec;
                        int next_slot = ((sec / 15) + 1) * 15;
                        int wait_sec = next_slot - sec;
                        if (wait_sec == 0) wait_sec = 15; // Si justo estamos en el segundo, esperar al siguiente ciclo

                        ESP_LOGI(TAG, "Esperando %d segundos para sincronizar inicio (T=%02d:%02d:%02d)...", 
                                 wait_sec, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
                        
                        char msg_wait[64];
                        snprintf(msg_wait, sizeof(msg_wait), "⏳ Esperando %ds para inicio...", wait_sec);
                        web_interface_send_log(msg_wait);

                        vTaskDelay(pdMS_TO_TICKS(wait_sec * 1000));

                        // Loguear hora de inicio real
                        gettimeofday(&tv, NULL);
                        timeinfo = localtime(&tv.tv_sec);
                        ESP_LOGI(TAG, "Inicio Ciclo: %02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

                        // Iniciar procesamiento
                        wav_start = test_files[current_file_idx].start;
                        wav_end = test_files[current_file_idx].end;
                        wav_pos = 44; // Skip header
                        wav_size = wav_end - wav_start;
                        
                        ESP_LOGI(TAG, "--- Procesando: %s ---", test_files[current_file_idx].name);
                        
                        char msg[64];
                        snprintf(msg, sizeof(msg), "▶️ Procesando: %s", test_files[current_file_idx].name);
                        web_interface_send_log(msg);
                        
                        monitor_reset(&mon);
                        current_state = FT8_STATE_RX;
                    } else {
                        // Fin del ciclo de test
                        ESP_LOGI(TAG, "TEST FINALIZADO.");
                        web_interface_send_log("✅ TEST FINALIZADO");
                        g_run_test_flag = false;
                        current_file_idx = 0;
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
#else
                // Sincronización en Modo Live (Microfono)
                struct timeval tv;
                gettimeofday(&tv, NULL);
                struct tm *timeinfo = localtime(&tv.tv_sec);
                int sec = timeinfo->tm_sec;
                int next_slot = ((sec / 15) + 1) * 15;
                int wait_sec = next_slot - sec;
                
                // Si falta menos de 1 segundo, esperar al siguiente ciclo para asegurar
                if (wait_sec < 1) wait_sec += 15;

                ESP_LOGI(TAG, "Sincronizando... Esperando %d segundos (Inicio: %02d:%02d:%02d)", 
                         wait_sec, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
                
                // Enviar estado a web
                char msg_wait[64];
                snprintf(msg_wait, sizeof(msg_wait), "⏳ Sincronizando (%ds)...", wait_sec);
                web_interface_send_log(msg_wait);

                // Esperar hasta el slot
                vTaskDelay(pdMS_TO_TICKS(wait_sec * 1000));
                
                // IMPORTANTE: Vaciar buffer de audio viejo antes de empezar
                audio_flush_rx();

                // Loguear inicio real
                gettimeofday(&tv, NULL);
                timeinfo = localtime(&tv.tv_sec);
                ESP_LOGI(TAG, "Inicio Ciclo Live: %02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
                web_interface_send_log("▶️ Escuchando...");

                monitor_reset(&mon);
                current_state = FT8_STATE_RX;
#endif
                break;

            case FT8_STATE_RX:
                // Procesar audio hasta llenar el waterfall o llegar al límite de tiempo
                // Limitar a 85 bloques (~13.6s) para dar tiempo a decodificar y sincronizar
                if (mon.wf.num_blocks >= 85) {
                    // Enviar Waterfall a la Web
                    size_t wf_size = mon.wf.num_blocks * mon.wf.block_stride;
                    web_interface_send_binary(mon.wf.mag, wf_size);

                    current_state = FT8_STATE_DECODE;
                    break;
                }

                // Leer un bloque de audio
                size_t bytes_to_read = mon.block_size * sizeof(int16_t);
                
#ifdef TEST_MODE_WAV
                if (wav_pos + bytes_to_read > wav_size) {
                    memset(pcm_buf, 0, bytes_to_read);
                } else {
                    memcpy(pcm_buf, wav_start + wav_pos, bytes_to_read);
                    wav_pos += bytes_to_read;

                    // Simular tiempo real de audio
                    uint32_t delay_ms = ((mon.block_size * 1000) / FT8_SAMPLE_RATE) / 2;
                    if (delay_ms < 2) delay_ms = 2; 
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }
                // Yield para evitar WDT en bucles largos
                if (mon.wf.num_blocks % 5 == 0) vTaskDelay(pdMS_TO_TICKS(1));
#else
                size_t read_len = 0;
                if (audio_read(pcm_buf, mon.block_size, &read_len) != ESP_OK || read_len == 0) {
                    // Si hay error o no se leyó nada (pausa), rellenar con silencio y esperar
                    memset(pcm_buf, 0, bytes_to_read);
                    vTaskDelay(pdMS_TO_TICKS(100)); // Evitar spin loop si está pausado
                }
#endif

                // Convertir a float y procesar
                for (int i = 0; i < mon.block_size; i++) {
#ifdef TEST_MODE_WAV
                    // Normalizar a rango [-1.0, 1.0] para ft8_lib
                    float_buf[i] = (float)pcm_buf[i] / 32768.0f;
#else
                    // Centrar y normalizar ADC (0-4095 -> -1.0 a 1.0)
                    // Ganancia por software x5.0 para mejorar visualización
                    float_buf[i] = (((float)pcm_buf[i] - 2048.0f) / 2048.0f) * 5.0f;
#endif
                }
                
                // Yield para evitar WDT antes de proceso pesado
                vTaskDelay(pdMS_TO_TICKS(1));
                monitor_process(&mon, float_buf);
                break;

            case FT8_STATE_DECODE:
                ESP_LOGI(TAG, "Iniciando decodificación... (Blocks: %d)", mon.wf.num_blocks);
                
                const int max_candidates = 20;
                ftx_candidate_t heap[20];
                int num_candidates = ftx_find_candidates(&mon.wf, max_candidates, heap, 10); // min_score = 10
                ESP_LOGI(TAG, "Candidatos encontrados: %d", num_candidates);

                // Deduplicación simple
                uint32_t decoded_hashes[20];
                int decoded_count = 0;

                for (int i = 0; i < num_candidates; i++) {
                    const ftx_candidate_t *cand = &heap[i];
                    ftx_message_t msg;
                    ftx_decode_status_t status;
                    
                    // Intentar decodificar
                    if (ftx_decode_candidate(&mon.wf, cand, 50, &msg, &status)) {
                        // Éxito en decodificación
                        char text[40]; // Buffer para el texto del mensaje
                        
                        // Desempaquetar mensaje a texto
                        ftx_message_offsets_t offsets;
                        
                        ftx_message_decode(&msg, NULL, text, &offsets);

                        // Calcular hash simple para deduplicación
                        uint32_t hash = 0;
                        for (char *p = text; *p; p++) hash = hash * 31 + *p;

                        // Verificar duplicados
                        bool is_duplicate = false;
                        for (int k = 0; k < decoded_count; k++) {
                            if (decoded_hashes[k] == hash) {
                                is_duplicate = true;
                                break;
                            }
                        }

                        if (!is_duplicate) {
                            if (decoded_count < 20) decoded_hashes[decoded_count++] = hash;
                            ESP_LOGI(TAG, "DECODIFICADO: %s | Score: %d | DT: %.2f", 
                                     text, cand->score, (float)cand->time_offset * mon.symbol_period);
                            
                            // Enviar a Web Interface
                            char web_msg[128];
                            snprintf(web_msg, sizeof(web_msg), "📡 RX: %s (%.1f dB)", text, (float)cand->score);
                            web_interface_send_log(web_msg);
                        }
                    } else {
                        // Fallo en decodificación (CRC o LDPC)
                        // Solo loguear si el score es alto para evitar ruido
                        if (cand->score > 15) {
                            char text[40] = "???";
                            // Intentar decodificar "best effort" para ver qué era
                            ftx_message_offsets_t offsets;
                            ftx_message_decode(&msg, NULL, text, &offsets);

                            ESP_LOGW(TAG, "[FALLO] %s | Score: %d | DT: %.2f | LDPC: %d | CRC: 0x%04X", 
                                     text, cand->score, (float)cand->time_offset * mon.symbol_period, 
                                     status.ldpc_errors, status.crc_extracted);
                        }
                    }
                    // Yield para evitar WDT
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                
                current_state = FT8_STATE_REPORT;
                break;

            case FT8_STATE_REPORT:
                {
                    struct timeval tv_end;
                    gettimeofday(&tv_end, NULL);
                    struct tm *timeinfo_end = localtime(&tv_end.tv_sec);
                    ESP_LOGI(TAG, "Fin Ciclo: %02d:%02d:%02d", timeinfo_end->tm_hour, timeinfo_end->tm_min, timeinfo_end->tm_sec);
                }

#ifdef TEST_MODE_WAV
                current_file_idx++;
#else
                vTaskDelay(pdMS_TO_TICKS(1000)); 
#endif
                current_state = FT8_STATE_IDLE;
                break;
        }
    }
    
    monitor_free(&mon);
    free(float_buf);
    free(pcm_buf);
}
