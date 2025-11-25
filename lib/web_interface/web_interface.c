/*
   Proyecto Módem FT8 - Gonza (Sistema y UI)
   Versión: Integrada como componente
*/
#include "web_interface.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include <esp_http_server.h>
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"
#include <sys/time.h>
#include <time.h>

// --- TUS DATOS DE RED ---
#define NOMBRE_RED      "ESP32-FT8-Modem"
#define PASSWORD_RED    "12345678"
#define CANAL_WIFI      1
#define MAX_CONEXIONES  4
#define LED_GPIO        2

static const char *TAG = "WEB_IF";

// Handle global del servidor
static httpd_handle_t server = NULL;
// Handle del cliente WebSocket (asumimos uno solo por simplicidad para este prototipo)
static int g_client_fd = -1;
// Callback de login
static web_login_cb_t g_login_cb = NULL;
// Callbacks de proveedores de datos
static web_get_data_cb_t g_get_call_cb = NULL;
static web_get_data_cb_t g_get_grid_cb = NULL;

/* ... (WiFi functions omitted for brevity) ... */

/* ===============================================================
   2. SERVIDOR WEB Y WEBSOCKETS
   =============================================================== */

// ... (HTML handler omitted) ...


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "¡Cliente conectado! MAC: "MACSTR", ID: %d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente desconectado. MAC: "MACSTR", ID: %d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void iniciar_wifi_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = NOMBRE_RED,
            .ssid_len = strlen(NOMBRE_RED),
            .channel = CANAL_WIFI,
            .password = PASSWORD_RED,
            .max_connection = MAX_CONEXIONES,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = { .required = true },
        },
    };
    
    if (strlen(PASSWORD_RED) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Access Point iniciado correctamente. SSID: %s", NOMBRE_RED);
}

/* ===============================================================
   2. SERVIDOR WEB Y WEBSOCKETS
   =============================================================== */

// Referencias al archivo HTML embebido
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// Handler de la página web "/"
static esp_err_t pagina_inicio_handler(httpd_req_t *req)
{
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// Handler del WebSocket "/ws"
static esp_err_t echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake WebSocket realizado");
        g_client_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    ws_pkt.type = HTTPD_WS_TYPE_TEXT; 

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Recibido por WS: %s", ws_pkt.payload);
        
        // Actualizamos el FD del cliente activo
        g_client_fd = httpd_req_to_sockfd(req);

        // Lógica de Mensajes
        char *payload = (char*)ws_pkt.payload;
        
        if (strcmp(payload, "TOGGLE_LED") == 0) {
            static int led_state = 0;
            led_state = !led_state;
            gpio_set_level(LED_GPIO, led_state);
            
            const char* msg = led_state ? "LED ENCENDIDO" : "LED APAGADO";
            web_interface_send_log(msg);
            
        } else if (strncmp(payload, "LOGIN:", 6) == 0) {
            // Formato: LOGIN:CALLSIGN:GRID
            char *call = strtok(payload + 6, ":");
            char *grid = strtok(NULL, ":");
            
            if (call && grid) {
                ESP_LOGI(TAG, "Login recibido: %s / %s", call, grid);
                if (g_login_cb) {
                    g_login_cb(call, grid);
                }
                web_interface_send_log("Login OK. Datos recibidos.");
            }
            
        } else if (strncmp(payload, "SYNC_TIME:", 10) == 0) {
            long timestamp = atol(payload + 10);
            if (timestamp > 0) {
                struct timeval tv;
                tv.tv_sec = timestamp;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);

                // Obtener hora actual UTC
                time_t now;
                struct tm timeinfo;
                time(&now);
                gmtime_r(&now, &timeinfo);
                
                char time_str[64];
                strftime(time_str, sizeof(time_str), "Tiempo sincronizado: %H:%M:%S", &timeinfo);
                web_interface_send_log(time_str);
            }

        } else if (strcmp(payload, "GET_CALLSIGN") == 0) {
            if (g_get_call_cb) {
                char buf[32];
                g_get_call_cb(buf, sizeof(buf));
                char msg[64];
                snprintf(msg, sizeof(msg), "Callsign actual: %s", buf);
                web_interface_send_log(msg);
            } else {
                web_interface_send_log("Error: Proveedor de Callsign no registrado");
            }
            
        } else if (strcmp(payload, "GET_GRID") == 0) {
            if (g_get_grid_cb) {
                char buf[32];
                g_get_grid_cb(buf, sizeof(buf));
                char msg[64];
                snprintf(msg, sizeof(msg), "Grid actual: %s", buf);
                web_interface_send_log(msg);
            } else {
                web_interface_send_log("Error: Proveedor de Grid no registrado");
            }

        } else {
            // Echo normal
            char respuesta[100];
            sprintf(respuesta, "ESP32 Recibió: %s", payload);
            
            // Reutilizamos ws_pkt para enviar
            ws_pkt.payload = (uint8_t*)respuesta;
            ws_pkt.len = strlen(respuesta);
            ws_pkt.type = HTTPD_WS_TYPE_TEXT; 
            httpd_ws_send_frame(req, &ws_pkt);
        }

        free(buf);
    }
    return ESP_OK;
}

static const httpd_uri_t ws = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = echo_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

static const httpd_uri_t ruta_inicio = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = pagina_inicio_handler,
    .user_ctx  = NULL
};

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Arrancando servidor web...");

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "¡Servidor Web Iniciado!");
        httpd_register_uri_handler(server, &ruta_inicio);
        httpd_register_uri_handler(server, &ws);
    }
}

/* ===============================================================
   API PÚBLICA
   =============================================================== */

void web_interface_init(void)
{
    ESP_LOGI(TAG, "Inicializando Interfaz Web...");
    
    // Configurar LED
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Inicializar Netif y Event Loop si no se ha hecho antes
    // (Asumimos que nvs_flash_init ya se llamó en main)
    esp_netif_init();
    esp_event_loop_create_default();

    iniciar_wifi_ap();
    start_webserver();
}

void web_interface_set_login_callback(web_login_cb_t cb)
{
    g_login_cb = cb;
}

void web_interface_register_data_providers(web_get_data_cb_t get_call, web_get_data_cb_t get_grid)
{
    g_get_call_cb = get_call;
    g_get_grid_cb = get_grid;
}

void web_interface_send_log(const char *msg)
{
    if (server == NULL || g_client_fd < 0) {
        return;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)msg;
    ws_pkt.len = strlen(msg);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(server, g_client_fd, &ws_pkt);
}
