#include <stdio.h>
#include <string.h>
#include <inttypes.h>  // For PRIu64
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_netif.h"

#define AP_SSID "your_ssid"         // Replace with your Wi-Fi SSID
#define AP_PASSWORD "your_password" // Replace with your Wi-Fi password
#define AP_MAX_CONNECTIONS 4
#define AP_CHANNEL 1                // Use channel 1 to match the mesh nodes
#define MONITOR_PORT 5000

static const char *TAG = "monitor_node";

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_AP_STACONNECTED)
        {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "Station connected: " MACSTR ", AID=%d", MAC2STR(event->mac), event->aid);
        }
        else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
        {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "Station disconnected: " MACSTR ", AID=%d", MAC2STR(event->mac), event->aid);
        }
    }
}

static void udp_server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting UDP server task...");

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(MONITOR_PORT);
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Receive from any IP

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", MONITOR_PORT);

    while (1)
    {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);

        // Increase buffer size to accommodate large CSI data
        char recv_buffer[4096];
        int len = recvfrom(sock, recv_buffer, sizeof(recv_buffer) - 1, 0, (struct sockaddr *)&source_addr, &addr_len);

        if (len < 0)
        {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }
        else
        {
            recv_buffer[len] = '\0'; // Null-terminate the string

            // Output the received data over the serial port
            ESP_LOGI(TAG, "Received data from %s: %s", inet_ntoa(source_addr.sin_addr), recv_buffer);
            // Or use printf if preferred
            printf("Received data from %s: %s\n", inet_ntoa(source_addr.sin_addr), recv_buffer);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

static void wifi_init_softap(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi as AP...");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);

    // Register event handler for Wi-Fi events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    // Optional: Set a static IP address for the AP (usually 192.168.4.1 by default)
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton("192.168.4.1");
    ip_info.gw.addr = esp_ip4addr_aton("192.168.4.1");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = AP_CHANNEL
        },
    };

    if (strlen(AP_PASSWORD) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started with SSID: %s", AP_SSID);
}

void app_main()
{
    // Set log level to INFO
    esp_log_level_set("*", ESP_LOG_INFO);

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize Wi-Fi as AP
    wifi_init_softap();

    // Start UDP server task with increased stack size
    xTaskCreate(udp_server_task, "udp_server", 8192, NULL, 5, NULL);
}