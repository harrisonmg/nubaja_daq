#ifndef NUBAJA_WIFI
#define NUBAJA_WIFI

/* 
* includes
*/ 
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "nubaja_udp_server.h"

/* 
* defines
*/ 

/* 
* globals
*/ 
const char* ssid = "DADS_ONLY";
const char* password = "";    
extern SemaphoreHandle_t commsSemaphore;                      

// Event Loop Handler
esp_err_t event_handler(void *ctx, system_event_t *event)
{
    const char *WIFI_TAG = "NUBAJA_WIFI";
    ESP_LOGI(WIFI_TAG, "event_handler");
    int ret;
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(WIFI_TAG, "WiFi Driver started. Connecting WiFi.");
            ret = esp_wifi_connect();
            if (ESP_OK != ret) {
                ESP_LOGE(WIFI_TAG, "connect failed");
            }
            else if (ESP_OK == ret) {
                ESP_LOGI(WIFI_TAG, "connect successful");
            }             
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGI(WIFI_TAG, "connected, DHCP client starting");           
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(WIFI_TAG, "connected, UPD server next");
            DHCP_IP = ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip);
            ESP_LOGI(WIFI_TAG, "got ip:%s\n", DHCP_IP);    
            ret = udp_server( commsSemaphore );
            if (ESP_OK != ret) {
                ESP_LOGE(WIFI_TAG, "UDP server failed");
            }             
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGE(WIFI_TAG, "disconnected");
            vTaskDelete( NULL );
            break;                        
        default:
            break;
    }
    return ESP_OK;
}

void wifi_config () {
    ESP_ERROR_CHECK( nvs_flash_init() );

    // Connect to the AP in STA mode
    tcpip_adapter_init();
    
    // Set Event Handler
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t sta_config = { };
    strcpy((char*)sta_config.sta.ssid, ssid);
    strcpy((char*)sta_config.sta.password, password);
    sta_config.sta.bssid_set = 0;
    sta_config.sta.channel = 0;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );  
}

#endif