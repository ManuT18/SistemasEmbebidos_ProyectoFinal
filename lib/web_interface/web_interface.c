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
// Array de clientes WebSocket
#define MAX_CLIENTS 4
static int g_client_fds[MAX_CLIENTS];

// Rol de Maestro
static int g_master_fd = -1;
static char g_master_call[16] = {0};
static char g_master_grid[8] = {0};

// Variables globales para callbacks
static web_login_cb_t g_login_cb = NULL;
static web_get_data_cb_t g_get_call_cb = NULL;
static web_get_data_cb_t g_get_grid_cb = NULL;
static web_test_cb_t g_test_cb = NULL;
static web_tx_cq_cb_t g_tx_cq_cb = NULL;
static web_set_freq_cb_t g_set_freq_cb = NULL;
static web_get_freq_cb_t g_get_freq_cb = NULL;

// Prototipos de funciones estáticas
static void init_clients(void);
static void add_client(int fd);
static void remove_client(int fd);
static void iniciar_wifi_ap(void);
static void start_webserver(void);

static void init_clients(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_client_fds[i] = -1;
    }
}

static void add_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_fds[i] == fd) return; // Ya existe
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_fds[i] == -1) {
            g_client_fds[i] = fd;
            return;
        }
    }
}

static void remove_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_fds[i] == fd) {
            g_client_fds[i] = -1;
        }
    }
    if (g_master_fd == fd) {
        // Si el maestro se desconecta abruptamente, ¿liberamos? 
        // Por seguridad, NO liberamos inmediatamente para evitar robo de sesión por desconexión temporal.
        // Solo liberamos con LOGOUT explícito.
        // g_master_fd = -1; 
    }
}

// Referencias al archivo HTML embebido
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// Handler de la página web "/"
static esp_err_t pagina_inicio_handler(httpd_req_t *req)
{
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// Event Handler para WiFi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Estación conectada: "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Estación desconectada: "MACSTR" leave, AID=%d",
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

// Implementación de web_interface_send_log (necesaria para echo_handler)
void web_interface_send_log(const char *msg)
{
    if (server == NULL) return;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)msg;
    ws_pkt.len = strlen(msg);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = g_client_fds[i];
        if (fd >= 0) {
            esp_err_t ret = httpd_ws_send_frame_async(server, fd, &ws_pkt);
            if (ret != ESP_OK) {
                remove_client(fd);
            }
        }
    }
}

// Handler del WebSocket "/ws"
static esp_err_t echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake WebSocket realizado");
        int fd = httpd_req_to_sockfd(req);
        add_client(fd);
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
        
        int current_fd = httpd_req_to_sockfd(req);
        add_client(current_fd); // Asegurar que está en la lista

        // Lógica de Mensajes
        char *payload = (char*)ws_pkt.payload;
        
        if (strncmp(payload, "LOGIN:", 6) == 0) {
            ESP_LOGI(TAG, "CMD LOGIN recibido: %s", payload);
            // Formato: LOGIN:CALLSIGN:GRID
            char *call = strtok(payload + 6, ":");
            char *grid = strtok(NULL, ":");
            
            if (call && grid) {
                ESP_LOGI(TAG, "Login Parseado: Call=%s, Grid=%s", call, grid);
                // Verificar estado de Maestro
                if (g_master_fd == -1) {
                    ESP_LOGI(TAG, "Sistema Libre. Asignando Maestro a FD=%d", current_fd);
                    // Sistema Libre -> Asignar Maestro
                    g_master_fd = current_fd;
                    strncpy(g_master_call, call, sizeof(g_master_call)-1);
                    strncpy(g_master_grid, grid, sizeof(g_master_grid)-1);
                    
                    // Guardar en NVS
                    if (g_login_cb) g_login_cb(call, grid);
                    
                    web_interface_send_log("Login OK. Eres el Maestro.");
                    
                } else {
                    ESP_LOGI(TAG, "Sistema Ocupado por %s. Verificando reconexión...", g_master_call);
                    // Sistema Ocupado -> Verificar credenciales
                    if (strcmp(g_master_call, call) == 0 && strcmp(g_master_grid, grid) == 0) {
                        // Es el maestro reconectándose
                        g_master_fd = current_fd;
                        web_interface_send_log("Login OK. Reconexión Maestro.");
                    } else {
                        ESP_LOGW(TAG, "Intento de acceso denegado (Ocupado). Ofreciendo espectador.");
                        // Es otro usuario -> RECHAZAR / OFRECER ESPECTADOR
                        // Enviar mensaje especial solo a este cliente
                        httpd_ws_frame_t resp;
                        memset(&resp, 0, sizeof(httpd_ws_frame_t));
                        char msg[64];
                        snprintf(msg, sizeof(msg), "LOGIN_BUSY:%s", g_master_call);
                        resp.payload = (uint8_t*)msg;
                        resp.len = strlen(msg);
                        resp.type = HTTPD_WS_TYPE_TEXT;
                        httpd_ws_send_frame(req, &resp);
                        
                        free(buf);
                        return ESP_OK;
                    }
                }
            } else {
                ESP_LOGE(TAG, "Error parseando LOGIN. Call o Grid nulos.");
            }
            
        } else if (strcmp(payload, "LOGOUT") == 0) {
             if (current_fd == g_master_fd) {
                 // Borrar datos
                 g_master_fd = -1;
                 memset(g_master_call, 0, sizeof(g_master_call));
                 memset(g_master_grid, 0, sizeof(g_master_grid));
                 
                 web_interface_send_log("LOGOUT_OK");
             }

        } else if (strcmp(payload, "TOGGLE_LED") == 0) {
            if (current_fd == g_master_fd) {
                static int led_state = 0;
                led_state = !led_state;
                gpio_set_level(LED_GPIO, led_state);
                web_interface_send_log(led_state ? "LED ENCENDIDO" : "LED APAGADO");
            }

        } else if (strcmp(payload, "TOGGLE_SLEEP") == 0) {
            if (current_fd == g_master_fd) {
                static int sleep_mode = 0;
                sleep_mode = !sleep_mode;
                if (sleep_mode) {
                    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
                    gpio_set_level(LED_GPIO, 0);
                    web_interface_send_log("💤 Modo Ahorro ACTIVADO");
                } else {
                    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                    web_interface_send_log("⚡ Modo Rendimiento ACTIVADO");
                }
            }

        } else if (strncmp(payload, "SYNC_TIME:", 10) == 0) {
            // Permitido a todos
            long timestamp = atol(payload + 10);
            if (timestamp > 0) {
                struct timeval tv;
                tv.tv_sec = timestamp;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
            }

        } else if (strcmp(payload, "GET_CALLSIGN") == 0) {
            // Permitido a todos (para ver quién está)
            if (g_get_call_cb) {
                char buf[32];
                g_get_call_cb(buf, sizeof(buf));
                char msg[64];
                snprintf(msg, sizeof(msg), "Callsign actual: %s", buf);
                web_interface_send_log(msg);
            }
            
        } else if (strcmp(payload, "GET_GRID") == 0) {
            if (g_get_grid_cb) {
                char buf[32];
                g_get_grid_cb(buf, sizeof(buf));
                char msg[64];
                snprintf(msg, sizeof(msg), "Grid actual: %s", buf);
                web_interface_send_log(msg);
            }

        } else if (strcmp(payload, "START_TEST") == 0) {
            if (current_fd == g_master_fd) {
                if (g_test_cb) {
                    g_test_cb();
                    web_interface_send_log("▶️ Test Iniciado...");
                }
            }
        } else if (strcmp(payload, "TX_CQ") == 0) {
            if (current_fd == g_master_fd) {
                if (g_tx_cq_cb) {
                    if (g_tx_cq_cb(g_master_call, g_master_grid)) {
                        // web_interface_send_log("Solicitud TX aceptada");
                    } else {
                        web_interface_send_log("⚠️ TX Ocupado o Error");
                    }
                } else {
                    web_interface_send_log("⚠️ Función TX no registrada");
                }
            }
        } else if (strncmp(payload, "SET_FREQ:", 9) == 0) {
            if (current_fd == g_master_fd) {
                int freq = atoi(payload + 9);
                if (freq >= 100 && freq <= 3000) {
                    if (g_set_freq_cb) {
                        g_set_freq_cb((uint16_t)freq);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "✅ Frecuencia TX ajustada a %d Hz", freq);
                        web_interface_send_log(msg);
                    } else {
                        web_interface_send_log("⚠️ Callback SET_FREQ no registrado");
                    }
                } else {
                    web_interface_send_log("⚠️ Frecuencia inválida (100-3000 Hz)");
                }
            }
        } else if (strcmp(payload, "GET_FREQ") == 0) {
            if (g_get_freq_cb) {
                uint16_t freq = g_get_freq_cb(); 
                char msg[32];
                snprintf(msg, sizeof(msg), "FREQ:%d", freq);
                
                httpd_ws_frame_t resp;
                memset(&resp, 0, sizeof(httpd_ws_frame_t));
                resp.payload = (uint8_t*)msg;
                resp.len = strlen(msg);
                resp.type = HTTPD_WS_TYPE_TEXT;
                httpd_ws_send_frame(req, &resp);
            }
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
    
    init_clients(); // Inicializar lista de clientes
    
    // Configurar LED
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Inicializar Netif y Event Loop si no se ha hecho antes
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

void web_interface_set_test_callback(web_test_cb_t cb)
{
    g_test_cb = cb;
}

void web_interface_set_tx_cq_callback(web_tx_cq_cb_t cb)
{
    g_tx_cq_cb = cb;
}

void web_interface_register_freq_callbacks(web_set_freq_cb_t set_freq, web_get_freq_cb_t get_freq)
{
    g_set_freq_cb = set_freq;
    g_get_freq_cb = get_freq;
}

void web_interface_send_binary(const uint8_t *data, size_t len)
{
    if (server == NULL) return;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = g_client_fds[i];
        if (fd >= 0) {
            esp_err_t ret = httpd_ws_send_frame_async(server, fd, &ws_pkt);
            if (ret != ESP_OK) {
                remove_client(fd);
            }
        }
    }
}
