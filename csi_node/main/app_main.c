#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "scan";

const char* esp_wifi_auth_mode_to_name(wifi_auth_mode_t auth_mode) {
    switch (auth_mode) {
        case WIFI_AUTH_OPEN: 
            return "WIFI_AUTH_OPEN";
        case WIFI_AUTH_WEP: 
            return "WIFI_AUTH_WEP";
        case WIFI_AUTH_WPA_PSK: 
            return "WIFI_AUTH_WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: 
            return "WIFI_AUTH_WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: 
            return "WIFI_AUTH_WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: 
            return "WIFI_AUTH_WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: 
            return "WIFI_AUTH_WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: 
            return "WIFI_AUTH_WPA2_WPA3_PSK";
        default: 
            return "Unknown";
    }
}

static void print_auth_mode(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_OPEN");
            break;
        case WIFI_AUTH_WEP:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WEP");
            break;
        case WIFI_AUTH_WPA_PSK:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WPA_PSK");
            break;
        case WIFI_AUTH_WPA2_PSK:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WPA2_PSK");
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WPA_WPA2_PSK");
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WPA2_ENTERPRISE");
            break;
        case WIFI_AUTH_WPA3_PSK:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WPA3_PSK");
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            ESP_LOGI(TAG, "Authmode : WIFI_AUTH_WPA2_WPA3_PSK");
            break;
        default:
            ESP_LOGI(TAG, "Authmode : Unknown");
            break;
    }
}

static void wifi_scan_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t apCount = 0;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&apCount)); 
        if (apCount == 0) {
            ESP_LOGI(TAG, "No WiFi found");
            return;
        }
        wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        if (ap_info == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for AP information");
            return;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_info));
        ESP_LOGI(TAG, "Found %u WiFi APs:", apCount);
        ESP_LOGI(TAG, "             SSID              | Channel | RSSI |   Auth Mode   ");
        ESP_LOGI(TAG, "----------------------------------------------------------------");
        for (int i = 0; i < apCount; i++) {
            print_auth_mode(ap_info[i].authmode);
            ESP_LOGI(TAG, "%32s | %7d | %4d | %s",
                   ap_info[i].ssid,
                   ap_info[i].primary,
                   ap_info[i].rssi,
                   (char *)esp_wifi_auth_mode_to_name(ap_info[i].authmode)); // Correct function name
        }
        free(ap_info);
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { 
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_scan_handler, NULL, NULL));

    // Start WiFi in station mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start WiFi scan
    wifi_scan_config_t scanConf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));

    // Keep the program running indefinitely
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}