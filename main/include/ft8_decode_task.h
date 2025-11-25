#pragma once

#include <stdint.h>

// Configuración de la tarea
#define FT8_DECODE_TASK_STACK_SIZE 8192
#define FT8_DECODE_TASK_PRIORITY   5

/**
 * @brief Tarea principal de decodificación FT8.
 * 
 * Se encarga de:
 * 1. Capturar audio (desde ADC o archivo WAV en modo test).
 * 2. Procesar FFT con sliding window (OSR=2).
 * 3. Generar waterfall.
 * 4. Buscar sincronización y decodificar mensajes FT8.
 * 
 * @param pvParameters Parámetros de FreeRTOS (no utilizado).
 */
void ft8_decode_task(void *pvParameters);
