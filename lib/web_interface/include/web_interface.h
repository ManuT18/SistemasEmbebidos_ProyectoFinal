#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Web Interface (WiFi AP + Web Server)
 */
void web_interface_init(void);

/**
 * @brief Send a log message to all connected WebSocket clients
 * 
 * @param msg Message string to send
 */
void web_interface_send_log(const char *msg);

/**
 * @brief Callback function type for login events
 */
typedef void (*web_login_cb_t)(const char *callsign, const char *grid);

/**
 * @brief Set the callback function to be called when a user logs in
 * 
 * @param cb Callback function
 */
void web_interface_set_login_callback(web_login_cb_t cb);

/**
 * @brief Callback type for providing data string
 * @param buffer Buffer to write data to
 * @param max_len Maximum length of buffer
 */
typedef void (*web_get_data_cb_t)(char *buffer, size_t max_len);

/**
 * @brief Register callbacks to provide data to the web interface
 * 
 * @param get_call Callback to get callsign
 * @param get_grid Callback to get grid locator
 */
void web_interface_register_data_providers(web_get_data_cb_t get_call, web_get_data_cb_t get_grid);

#ifdef __cplusplus
}
#endif
