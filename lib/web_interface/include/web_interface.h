#pragma once

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

#ifdef __cplusplus
}
#endif
