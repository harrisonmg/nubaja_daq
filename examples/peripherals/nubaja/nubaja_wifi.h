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
#include <lwip/sockets.h>

/* 
* defines
*/ 

#define DEVICE_IP          "192.168.43.69" // The IP address that we want our device to have.
#define DEVICE_GW          "192.168.43.1" // The Gateway address where we wish to send packets.
#define DEVICE_NETMASK     "255.255.255.0" // The netmask specification.

/* 
* globals
*/ 
const char* ssid = "DADS_ONLY";
const char* password = "correct horse battery staple";    



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
                ESP_LOGE(WIFI_TAG, "Connect failed");
            }
            else if (ESP_OK == ret) {
                ESP_LOGI(WIFI_TAG, "Connect successful");
            }             
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGI(WIFI_TAG, "connected, getting IP address");           
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(WIFI_TAG, "connected, starting UDP listener");
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
    tcpip_adapter_dhcpc_stop(0); // Don't run a DHCP client
    tcpip_adapter_ip_info_t ipInfo;

    inet_pton(AF_INET, DEVICE_IP, &ipInfo.ip);
    inet_pton(AF_INET, DEVICE_GW, &ipInfo.gw);
    inet_pton(AF_INET, DEVICE_NETMASK, &ipInfo.netmask);
    tcpip_adapter_set_ip_info(0, &ipInfo);

    
    // Set Event Handler
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );
    
    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // wifi_config_t sta_config = { };
    // strcpy((char*)sta_config.sta.ssid, ssid);
    // strcpy((char*)sta_config.sta.password, password);
    // sta_config.sta.bssid_set = 0;
    // sta_config.sta.channel = 0;
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta_config = { };
    strcpy((char*)sta_config.sta.ssid, ssid);
    strcpy((char*)sta_config.sta.password, password);
    sta_config.sta.bssid_set = 0;
    sta_config.sta.channel = 0;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );  
}

#endif