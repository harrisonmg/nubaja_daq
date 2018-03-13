#ifndef NUBAJA_UDP_SERVER
#define NUBAJA_UDP_SERVER


/* 
* includes
*/ 
#include <sys/socket.h>
#include <sys/unistd.h>
#include "esp_wifi.h"

/* 
* defines
*/ 

//WIFI
#define PORT_NUMBER                         22
#define BUFLEN                              512

/* 
* globals
*/ 

extern int comms_en;
extern int program_len;
extern char *DHCP_IP;

int get_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
    char WIFI_tag[]="udpserver";
    if(getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen) == -1) {
        ESP_LOGE(WIFI_tag, "getsockopt failed");
        return -1;
    }
    return result;
    
}

int show_socket_error_reason(int socket)
{
    int err = get_socket_error_code(socket);
    char WIFI_tag[]="udpserver";
    ESP_LOGE(WIFI_tag, "socket error %d %s", err, strerror(err));
    return err;
}

void close_socket(int socket)
{
    close(socket);
}

/* UDP Listener
 * expects packets delivered via the following, or equivalent: 
 * echo -n $cmd| nc -4u -q5 $IP $PORT for linux
 * echo -n $cmd| nc -4u -w5 $IP $PORT for mac
 */
esp_err_t udp_server( SemaphoreHandle_t commsSemaphore )
{
    char WIFI_tag[]="udpserver";
    int mysocket;
    struct sockaddr_in si_other;
    
    unsigned int slen = sizeof(si_other),recv_len;
    char buf[BUFLEN];
    
    // bind to socket
    ESP_LOGI(WIFI_tag, "bind_udp_server port:%d", PORT_NUMBER);
    mysocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mysocket < 0) {
        show_socket_error_reason(mysocket);
        return ESP_FAIL;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUMBER);
    server_addr.sin_addr.s_addr = inet_addr(DHCP_IP); //IP address
    ESP_LOGI(WIFI_tag, "socket ip:%s\n", inet_ntoa(server_addr.sin_addr.s_addr));

    if (bind(mysocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        show_socket_error_reason(mysocket);
        ESP_LOGI(WIFI_tag,"closing socket");
        close(mysocket);
    } else {
        ESP_LOGI(WIFI_tag,"socket created without errors");
         while(1) {
            ESP_LOGI(WIFI_tag,"Waiting for incoming data");
            memset(buf,0,BUFLEN);
            
            if ((recv_len = recvfrom(mysocket, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == -1)
            {
                ESP_LOGE(WIFI_tag,"recvfrom");
                break;
            }
            
            ESP_LOGI(WIFI_tag,"Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
            ESP_LOGI(WIFI_tag,"Command: %s -- %d\n" , buf, recv_len);
            // Set the NULL byte to avoid garbage in the read buffer
            if ((recv_len + 1) < BUFLEN) {
                buf[recv_len + 1] = '\0';
            }
                        
            if ( memcmp( buf, "start", recv_len) == 0) {
                ESP_LOGI(WIFI_tag,"Start Case\n");
                comms_en = 0;
                xSemaphoreGive(commsSemaphore);
                break; //exits while loop and program proceeds to task creation and normal operation
            }
            else if ( memcmp( "num", buf, recv_len) > 0) {
                ESP_LOGI(WIFI_tag,"Number Case\n");                
                int dec = 0, i, len;
                len = strlen(buf);
                for(i=0; i<len; i++){
                    dec = dec * 10 + ( buf[i] - '0' );
                } 
                ESP_LOGI(WIFI_tag,"Program length: %d\n" , dec);            
                program_len = dec;
                comms_en = 0;
                xSemaphoreGive(commsSemaphore);
                break;
            } 
            else {
                ESP_LOGE(WIFI_tag,"Command: %s\n", buf);
                break;
            }
        }

        ESP_LOGI(WIFI_tag,"command received - closing socket");
        close(mysocket);
        //turn off wifi so event handler is no longer active
        // esp_event_loop_init(NULL,NULL);
        esp_wifi_disconnect();
        esp_wifi_stop();
    }

    return ESP_OK;
}

#endif

