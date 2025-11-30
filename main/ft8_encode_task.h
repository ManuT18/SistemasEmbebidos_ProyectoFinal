#ifndef FT8_ENCODE_TASK_H
#define FT8_ENCODE_TASK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa la tarea de transmisión (si es necesario).
 * Por ahora, la transmisión se puede ejecutar bajo demanda en una tarea dedicada.
 */
void ft8_tx_init(void);

/**
 * @brief Configura la frecuencia base de transmisión.
 * @param freq Frecuencia en Hz (ej. 1000, 1500, 2000).
 */
void ft8_set_tx_freq(uint16_t freq);

/**
 * @brief Obtiene la frecuencia base de transmisión actual.
 * @return Frecuencia en Hz.
 */
uint16_t ft8_get_tx_freq(void);

/**
 * @brief Solicita la transmisión de un mensaje CQ.
 * 
 * @param callsign Callsign del emisor (ej. LU7AA)
 * @param grid Grid Locator del emisor (ej. GF05)
 * @return true si se aceptó la solicitud, false si ya está transmitiendo.
 */
bool ft8_tx_cq(const char *callsign, const char *grid);

/**
 * @brief Solicita la transmisión de un mensaje de texto libre.
 * 
 * @param msg Mensaje a transmitir (max 13 caracteres)
 * @return true si se aceptó la solicitud, false si ya está transmitiendo.
 */
bool ft8_tx_msg(const char *msg);

#endif // FT8_ENCODE_TASK_H
