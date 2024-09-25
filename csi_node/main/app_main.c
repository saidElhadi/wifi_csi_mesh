#include <stdio.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Define your peer information
static uint8_t peer_mac[] = {0x24, 0x6F, 0x28, 0xA5, 0xBC, 0x90}; // Change to your peer MAC address

// ESP-NOW send callback
void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    printf("Send status: %s\n", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ESP-NOW receive callback
void recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    printf("Received data from: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    printf("Data: %.*s\n", data_len, data);
}

void init_esp_now() {
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        printf("Error initializing ESP-NOW\n");
        return;
    }
    esp_now_register_send_cb(send_cb);
    esp_now_register_recv_cb(recv_cb);
    
    // Add a peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, peer_mac, 6);
    peer_info.channel = 0;  // Set the channel to the same for both devices
    peer_info.ifidx = ESP_IF_WIFI_STA;
    peer_info.encrypt = false;

    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        printf("Failed to add peer\n");
        return;
    }
}

void init_wifi() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize WiFi as a station
    tcpip_adapter_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

void app_main() {
    init_wifi();
    init_esp_now();

    // Example: sending data to the peer
    const char *message = "Hello, ESP-NOW!";
    esp_err_t result = esp_now_send(peer_mac, (uint8_t *)message, strlen(message));

    if (result == ESP_OK) {
        printf("Message sent\n");
    } else {
        printf("Send error\n");
    }

    // Add a delay to allow the device to receive and send data
    vTaskDelay(5000 / portTICK_PERIOD_MS);
}
