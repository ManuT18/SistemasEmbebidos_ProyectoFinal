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
 * @brief Solicita la transmisión de un mensaje CQ.
 * 
 * @param callsign Callsign del emisor (ej. LU7AA)
 * @param grid Grid Locator del emisor (ej. GF05)
 * @return true si se aceptó la solicitud, false si ya está transmitiendo.
 */
bool ft8_tx_cq(const char *callsign, const char *grid);

#endif // FT8_ENCODE_TASK_H
