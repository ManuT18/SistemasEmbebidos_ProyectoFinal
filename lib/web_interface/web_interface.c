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

/* ===============================================================
   1. GESTIÓN DE WI-FI (AP)
   =============================================================== */
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

const char* html_page = 
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Modem FT8</title>"
    "<style>"
    " body { background:#111; color:#0f0; font-family:monospace; text-align:center; }"
    " #container { display: flex; justify-content: center; align-items: flex-start; gap: 20px; padding: 20px; }"
    " #log { border:1px solid #333; height:300px; width: 60%; overflow:auto; padding:10px; text-align:left; background:#000; }"
    " #controls { display: flex; flex-direction: column; gap: 10px; }"
    " button { padding: 10px 20px; background: #0f0; color: #000; border: none; cursor: pointer; font-weight: bold; }"
    " button:hover { background: #0a0; }"
    "</style></head>"
    "<body>"
    " <h1>📡 Monitor FT8</h1>"
    " <div id='container'>"
    "   <div id='log'>Esperando conexión...</div>"
    "   <div id='controls'>"
    "     <button onclick='toggleLed()'>💡 Toggle LED</button>"
    "   </div>"
    " </div>"
    " <script>"
    "   var socket = new WebSocket('ws://' + location.hostname + '/ws');"
    "   socket.onopen = function() { "
    "       document.getElementById('log').innerHTML = '<p>✅ Conectado al ESP32</p>'; "
    "       socket.send('Hola ESP32, soy el Celular');"
    "   };"
    "   socket.onmessage = function(event) { "
    "       var log = document.getElementById('log');"
    "       log.innerHTML += '<p>' + event.data + '</p>';"
    "       log.scrollTop = log.scrollHeight;"
    "   };"
    "   function toggleLed() {"
    "       socket.send('TOGGLE_LED');"
    "   }"
    " </script>"
    "</body></html>";

// Handler de la página web "/"
static esp_err_t pagina_inicio_handler(httpd_req_t *req)
{
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
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

        // Lógica de Control de LED
        if (strcmp((char*)ws_pkt.payload, "TOGGLE_LED") == 0) {
            static int led_state = 0;
            led_state = !led_state;
            gpio_set_level(LED_GPIO, led_state);
            
            const char* msg = led_state ? "LED ENCENDIDO" : "LED APAGADO";
            web_interface_send_log(msg);
        } else {
            // Echo normal
            char respuesta[100];
            sprintf(respuesta, "ESP32 Recibió: %s", (char*)ws_pkt.payload);
            
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
