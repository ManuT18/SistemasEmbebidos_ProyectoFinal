#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

/**
 * @brief Callback function type for starting the test
 */
typedef void (*web_test_cb_t)(void);

/**
 * @brief Set the callback function to start the FT8 test
 * 
 * @param cb Callback function
 */
void web_interface_set_test_callback(web_test_cb_t cb);

/**
 * @brief Callback function type for TX CQ
 * @return true if accepted, false otherwise
 */
typedef bool (*web_tx_cq_cb_t)(const char *callsign, const char *grid);

/**
 * @brief Set the callback function to transmit CQ
 * 
 * @param cb Callback function
 */
void web_interface_set_tx_cq_callback(web_tx_cq_cb_t cb);

/**
 * @brief Send a binary message to the connected WebSocket client
 * 
 * @param data Pointer to the data buffer
 * @param len Length of the data
 */
void web_interface_send_binary(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
