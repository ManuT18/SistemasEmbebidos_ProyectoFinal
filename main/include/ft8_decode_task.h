/**
 * @file ft8_decode_task.h
 * @brief Tarea principal para la decodificación del protocolo FT8.
 * 
 * Este módulo gestiona el ciclo de vida completo de la recepción FT8:
 * - Captura de audio (desde ADC o archivos WAV de prueba).
 * - Procesamiento DSP (Ventana Hanning, FFT, Magnitud).
 * - Generación de Waterfall (Espectrograma).
 * - Sincronización temporal y decodificación de mensajes.
 * 
 * Implementa una máquina de estados finitos (FSM) para orquestar estas fases.
 */

#pragma once

#include <stdint.h>

/// Tamaño del stack para la tarea de decodificación (en bytes)
#define FT8_DECODE_TASK_STACK_SIZE 49152

/// Prioridad de la tarea de decodificación (Mayor prioridad que tareas de UI/Red)
#define FT8_DECODE_TASK_PRIORITY   5

/**
 * @brief Punto de entrada de la tarea de decodificación FT8.
 * 
 * Esta función debe ser invocada mediante xTaskCreate().
 * Ejecuta un bucle infinito que implementa la máquina de estados:
 * - IDLE: Espera el inicio del slot de 15 segundos.
 * - RX: Captura y procesa audio en tiempo real (o simulado).
 * - DECODE: Ejecuta el algoritmo de decodificación FT8.
 * - REPORT: Muestra los mensajes decodificados por consola.
 * 
 * @param pvParameters Parámetros de FreeRTOS (no utilizado actualmente).
 */
void ft8_decode_task(void *pvParameters);
