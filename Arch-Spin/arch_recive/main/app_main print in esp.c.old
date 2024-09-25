/* Receiving Node Code with Adjustments */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_timer.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define AP_SSID "your_ssid"           // Replace with your monitoring node's SSID
#define AP_PASSWORD "your_password"   // Replace with your monitoring node's password
#define MONITOR_IP_ADDR "192.168.4.1" // Monitoring node's IP address
#define MONITOR_PORT 5000             // Monitoring node's UDP port

#define TAG_NUMBER 3 // Unique tag number for this node

static const char *TAG = "receiving_node";

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_event_group;

/* Queue for CSI data */
#define CSI_QUEUE_SIZE 500 // Increased queue size
static QueueHandle_t csi_queue;

/* Maximum expected CSI data length */
#define MAX_CSI_DATA_LEN 512

/* Buffer pool definitions */
#define BUFFER_POOL_SIZE 20                           // Adjust based on expected load
#define DATA_BUFFER_SIZE (MAX_CSI_DATA_LEN * 5 + 100) // Each data point can take up to 5 chars

typedef struct
{
    char data[DATA_BUFFER_SIZE];
    bool in_use;
} data_buffer_t;

static data_buffer_t buffer_pool[BUFFER_POOL_SIZE];

/* Function to get a free buffer from the pool */
static data_buffer_t *get_free_buffer()
{
    for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    {
        if (!buffer_pool[i].in_use)
        {
            buffer_pool[i].in_use = true;
            return &buffer_pool[i];
        }
    }
    return NULL; // No free buffer available
}

/* Wi-Fi Event Handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            esp_wifi_connect();
            ESP_LOGI(TAG, "Retrying connection to the AP");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Initialize Wi-Fi in Station Mode */
static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.ampdu_tx_enable = false;
    cfg.ampdu_rx_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Disable power-saving mode
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Register Event Handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Configure Wi-Fi connection
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = AP_SSID,
            .password = AP_PASSWORD,
            .listen_interval = 3, // Adjust as needed
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi initialization completed.");

    // Wait for connection
    xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT,
                        pdFALSE,
                        pdFALSE,
                        portMAX_DELAY);

    // Verify the current channel
    uint8_t primary_channel;
    wifi_second_chan_t second_channel;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&primary_channel, &second_channel));
    ESP_LOGI(TAG, "Operating on channel: %d", primary_channel);
}

/* ESP-NOW Receive Callback */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    ESP_LOGI(TAG, "ESP-NOW packet received from " MACSTR ", length: %d",
             MAC2STR(recv_info->src_addr), data_len);

    // No additional processing needed here
}

/* CSI Receive Callback */
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len == 0)
    {
        ESP_LOGW(TAG, "Invalid CSI data received: info=%p, buf=%p, len=%d", info, info ? info->buf : NULL, info ? info->len : -1);
        return;
    }

    // Throttle CSI data processing
    static uint64_t last_csi_time = 0;
    static const uint64_t csi_interval = 100000; // Collect CSI every 100ms (adjust as needed)
    uint64_t current_time = esp_timer_get_time();
    if (current_time - last_csi_time < csi_interval)
    {
        // Skip this CSI data
        return;
    }
    last_csi_time = current_time;

    data_buffer_t *buf = get_free_buffer();
    if (!buf)
    {
        ESP_LOGW(TAG, "No free buffer available");
        return;
    }

    char *data_buffer = buf->data;
    int offset = 0;

    // Get timestamp (in microseconds)
    uint64_t timestamp = esp_timer_get_time();

    // Build the data string in the format: tag:timestamp:csi
    offset += snprintf(data_buffer + offset, DATA_BUFFER_SIZE - offset, "%d:%llu:", TAG_NUMBER, (unsigned long long)timestamp);

    // Append CSI data as comma-separated values
    for (int i = 0; i < info->len && offset < DATA_BUFFER_SIZE - 6; i++)
    {
        int n = snprintf(data_buffer + offset, DATA_BUFFER_SIZE - offset, "%d,", info->buf[i]);
        if (n < 0 || n >= (int)(DATA_BUFFER_SIZE - offset))
        {
            ESP_LOGW(TAG, "Error formatting CSI data or buffer overflow");
            break;
        }
        offset += n;
    }

    // Remove the last comma
    if (offset > 0 && data_buffer[offset - 1] == ',')
    {
        data_buffer[offset - 1] = '\0';
    }
    else
    {
        data_buffer[offset] = '\0';
    }

    // Send buffer to queue
    if (xQueueSend(csi_queue, &buf, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to send CSI data to queue");
        buf->in_use = false; // Release buffer back to pool
    }
}

/* Initialize CSI Reception */
static void wifi_csi_init()
{
    // Configure CSI
    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = false, // Accept all channels
        .manu_scale = false,
        .shift = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

/* Initialize ESP-NOW */
static void espnow_init()
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
}

/* UDP Client Task */
static void udp_client_task(void *pvParameters)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(MONITOR_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(MONITOR_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket created");

    ESP_LOGI(TAG, "UDP client task started");

    data_buffer_t *buf;
    while (1)
    {
        if (xQueueReceive(csi_queue, &buf, portMAX_DELAY) == pdTRUE)
        {
            // Send data via UDP
            int err = sendto(sock, buf->data, strlen(buf->data), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0)
            {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            }
            else
            {
                // Log summary information
                ESP_LOGI(TAG, "Data sent: length=%d bytes", strlen(buf->data));
            }
            buf->in_use = false; // Release buffer back to pool
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

void app_main()
{
    // Increase logging level for debugging
    esp_log_level_set("*", ESP_LOG_INFO);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi and connect to the monitoring node's AP
    wifi_init_sta();

    // Initialize ESP-NOW
    espnow_init();

    // Initialize CSI reception
    wifi_csi_init();

    // Create queue for CSI data
    csi_queue = xQueueCreate(CSI_QUEUE_SIZE, sizeof(data_buffer_t *));

    // Start UDP client task with increased stack size and priority
    xTaskCreate(udp_client_task, "udp_client", 8192, NULL, 10, NULL);
}
