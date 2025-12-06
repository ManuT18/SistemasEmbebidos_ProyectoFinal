/*
   Proyecto Módem FT8 - Gonza (Sistema y UI)
   Versión: Corregida FINAL (HTTPD_WS_TYPE_TEXT)
*/
#include <string.h>
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

// --- TUS DATOS DE RED ---
#define NOMBRE_RED      "ESP32-FT8-Modem"
#define PASSWORD_RED    "12345678"
#define CANAL_WIFI      1
#define MAX_CONEXIONES  4

static const char *TAG = "WIFI_GONZA";

// Handle global del servidor
static httpd_handle_t server = NULL;

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

void iniciar_wifi_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
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
    " #log { border:1px solid #333; height:300px; overflow:auto; padding:10px; text-align:left; background:#000; }"
    "</style></head>"
    "<body>"
    " <h1>📡 Monitor FT8</h1>"
    " <div id='log'>Esperando conexión...</div>"
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
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    // --- CORRECCIÓN AQUÍ: HTTPD_WS_TYPE_TEXT ---
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
        
        // Respondemos lo mismo (Echo)
        char respuesta[100];
        sprintf(respuesta, "ESP32 Recibió: %s", (char*)ws_pkt.payload);
        ws_pkt.payload = (uint8_t*)respuesta;
        ws_pkt.len = strlen(respuesta);
        
        // --- Y AQUÍ TAMBIÉN ---
        ws_pkt.type = HTTPD_WS_TYPE_TEXT; 
        
        httpd_ws_send_frame(req, &ws_pkt);
        free(buf);
    }
    return ESP_OK;
}

// Definimos la ruta del WebSocket con is_websocket = true
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

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Arrancando servidor web...");

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "¡Servidor Web Iniciado!");
        httpd_register_uri_handler(server, &ruta_inicio);
        httpd_register_uri_handler(server, &ws);
        return server;
    }
    return NULL;
}

// Tarea de Simulación DSP
static void tarea_simulacion_dsp(void *pvParameters)
{
    char mensaje[64];
    int contador = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(3000)); // Esperar 3 segundos

        sprintf(mensaje, "CQ DX LU7AA %d dB", -10 + (contador % 5));
        contador++;

        ESP_LOGI("DSP_SIM", "Decodificado: %s", mensaje);
    }
}

/* ===============================================================
   3. MAIN PRINCIPAL
   =============================================================== */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "--- INICIANDO MODEM FT8 ---");
    
    // 1. Wi-Fi
    iniciar_wifi_ap();

    // 2. Web Server
    start_webserver();

    // 3. Arrancar Tarea de Simulación
    xTaskCreate(tarea_simulacion_dsp, "SimulacionDSP", 4096, NULL, 5, NULL);
}