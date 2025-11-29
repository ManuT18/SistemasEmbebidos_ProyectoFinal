#include "ft8_encode_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_driver.h"
#include "ft8/encode.h"
#include "ft8/message.h"
#include "ft8/constants.h"
#include "web_interface.h" // Para enviar logs a la web
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "FT8_TX";

// Configuración de Audio
// Configuración de Audio
#define TX_SAMPLE_RATE 12000
// #define TX_BASE_FREQ   1000.0f // Reemplazado por variable
#define SYMBOL_PERIOD  0.160f  // Duración de un símbolo FT8 (segundos)
#define SAMPLES_PER_SYMBOL 1920 // (int)(TX_SAMPLE_RATE * SYMBOL_PERIOD)

// Variables de estado
static volatile bool g_tx_busy = false;
static char g_tx_call[16];
static char g_tx_grid[8];
static uint16_t g_tx_base_freq = 1000; // Frecuencia por defecto

// ... (audio_buf definition) ...

void ft8_set_tx_freq(uint16_t freq) {
    if (freq >= 100 && freq <= 3000) {
        g_tx_base_freq = freq;
        ESP_LOGI(TAG, "Frecuencia TX configurada a %d Hz", freq);
    }
}

uint16_t ft8_get_tx_freq(void) {
    return g_tx_base_freq;
}

// ... (ft8_tx_task definition) ...

// Buffer de audio para un símbolo (1920 muestras * 2 bytes = 3840 bytes)
// Usamos int16_t para compatibilidad con audio_driver
static int16_t audio_buf[SAMPLES_PER_SYMBOL];

/**
 * @brief Tarea que maneja la secuencia de transmisión.
 * Se crea dinámicamente o se despierta cuando hay que transmitir.
 */
static void ft8_tx_task(void *arg)
{
    ESP_LOGI(TAG, "Iniciando Transmisión CQ: %s %s", g_tx_call, g_tx_grid);
    
    // --- Sincronización con Slot de Tiempo (00, 15, 30, 45) ---
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    int64_t now_us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
    int64_t next_slot_sec = ((tv.tv_sec / 15) + 1) * 15;
    int64_t next_slot_us = next_slot_sec * 1000000LL;
    
    int64_t wait_us = next_slot_us - now_us;
    
    // Si falta menos de 1 segundo, saltamos al siguiente slot (15s más)
    if (wait_us < 1000000LL) {
        wait_us += 15000000LL;
    }

    // Compensación de Overhead (Driver startup ~100-200ms)
    // Despertamos 200ms ANTES del slot para iniciar el driver
    int64_t overhead_comp_us = 200000LL; 
    if (wait_us > overhead_comp_us) {
        wait_us -= overhead_comp_us;
    } else {
        wait_us = 0; // Ya estamos tarde, iniciar ya
    }

    int wait_ms = (int)(wait_us / 1000);
    ESP_LOGI(TAG, "Esperando %d ms para inicio de slot TX (con compensación)...", wait_ms);
    
    char msg_wait[64];
    snprintf(msg_wait, sizeof(msg_wait), "⏳ TX Programada en %.1fs...", wait_ms / 1000.0);
    web_interface_send_log(msg_wait);

    vTaskDelay(pdMS_TO_TICKS(wait_ms));

    web_interface_send_log("📡 TX: Generando mensaje CQ...");

    // 1. Codificar Mensaje
    ftx_message_t msg;
    ftx_message_init(&msg);
    
    // Codificar "CQ CALL GRID" (Standard Message)
    // ftx_message_encode_std intenta adivinar si es CQ, DE, etc.
    // Para CQ específico: call_to="CQ", call_de=g_tx_call, extra=g_tx_grid
    ftx_message_rc_t rc = ftx_message_encode_std(&msg, NULL, "CQ", g_tx_call, g_tx_grid);
    
    if (rc != FTX_MESSAGE_RC_OK) {
        ESP_LOGE(TAG, "Error codificando mensaje: %d", rc);
        web_interface_send_log("❌ Error codificando mensaje");
        g_tx_busy = false;
        vTaskDelete(NULL);
        return;
    }

    // 2. Generar Tonos (Símbolos)
    uint8_t tones[FT8_NN]; // 79 tonos
    ft8_encode(msg.payload, tones);

    web_interface_send_log("📡 TX: Transmitiendo...");

    // Preparar driver para TX (Half-Duplex)
    audio_tx_start();

    // 3. Sintetizar y Enviar Audio (CP-FSK)
    // Fase acumulada para mantener continuidad
    float phase = 0.0f;
    const float two_pi = 6.28318530718f;
    const float dt = 1.0f / TX_SAMPLE_RATE;

    // Pre-calcular amplitud máxima (DAC 8-bit -> 0..255, audio_driver escala int16)
    // Usamos amplitud completa de int16 (+-32767)
    const float amplitude = 30000.0f; 

    for (int i = 0; i < FT8_NN; i++) {
        // Frecuencia del tono actual
        // FT8 spacing = 6.25 Hz
        float freq = (float)g_tx_base_freq + (tones[i] * 6.25f);
        float d_phase = two_pi * freq * dt;

        // Generar muestras para este símbolo
        for (int k = 0; k < SAMPLES_PER_SYMBOL; k++) {
            audio_buf[k] = (int16_t)(amplitude * sinf(phase));
            phase += d_phase;
            if (phase >= two_pi) phase -= two_pi;
        }

        // Enviar al DAC (Bloqueante)
        size_t written = 0;
        audio_write(audio_buf, SAMPLES_PER_SYMBOL, &written);
        
        // Yield para evitar watchdog
        vTaskDelay(1);
    }

    // Silencio final / Ramp down (opcional, por ahora corte abrupto)
    // Enviar un poco de silencio para vaciar buffers
    memset(audio_buf, 0, sizeof(audio_buf));
    size_t written;
    audio_write(audio_buf, SAMPLES_PER_SYMBOL, &written);

    ESP_LOGI(TAG, "Transmisión Finalizada");
    web_interface_send_log("✅ TX Finalizada");

    // Restaurar driver a RX
    audio_tx_stop();

    g_tx_busy = false;
    vTaskDelete(NULL); // Auto-eliminar tarea
}

bool ft8_tx_cq(const char *callsign, const char *grid)
{
    if (g_tx_busy) {
        return false; // Ya hay una transmisión en curso
    }

    if (!callsign || !grid) return false;

    // Copiar datos
    strncpy(g_tx_call, callsign, sizeof(g_tx_call)-1);
    strncpy(g_tx_grid, grid, sizeof(g_tx_grid)-1);
    
    g_tx_busy = true;

    // Crear tarea para no bloquear el contexto actual (Web Server)
    xTaskCreate(ft8_tx_task, "ft8_tx_task", 16384, NULL, 5, NULL);

    return true;
}

void ft8_tx_init(void)
{
    // Nada especial por ahora
}
