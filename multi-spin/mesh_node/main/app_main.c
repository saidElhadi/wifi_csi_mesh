#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "system_params.h"
#include "esp_mac.h"

#define AP_SSID "your_ssid"         // Replace with your Wi-Fi SSID
#define AP_PASSWORD "your_password" // Replace with your Wi-Fi password
#define MONITOR_IP "192.168.4.1"    // Replace with your monitor's IP
#define MONITOR_PORT 5000
#define BROADCAST_INTERVAL 2000 // 2 seconds

#define MAX_NUM_NODES 3
#define CHANNELS_NUMBER 1

#define CSI_DATA_LEN 512

// Set this to the node's index (0, 1, or MAX_NUM_NODES - 1)
static const int tag_number = 0;

static const char *TAG = "node";
static int sock = -1;
static QueueHandle_t csi_data_queue;
static int node_index = -1;
static EventGroupHandle_t wifi_event_group;
const int IP_BIT = BIT0;

uint8_t current_broadcaster = 255; // Initialize to 255, meaning no broadcaster
const uint8_t node_mac_addresses[MAX_NUM_NODES][6] = {
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x02}};
const uint8_t channel_sequence[CHANNELS_NUMBER] = {1};

typedef struct
{
    uint8_t tagnumber;
    uint64_t timestamp;
    uint16_t len;
    int8_t data[CSI_DATA_LEN]; // CSI data is signed
} csi_data_t;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP Address: %s", ip4addr_ntoa(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, IP_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, node_mac_addresses[tag_number]));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = AP_SSID,
            .password = AP_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
}

static void udp_socket_init()
{
    if (sock != -1)
    {
        close(sock);
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        sock = -1;
    }
    else
    {
        ESP_LOGI(TAG, "UDP socket created successfully");
    }
}

static void send_csi_data(const csi_data_t *csi_data)
{
    if (sock == -1)
    {
        ESP_LOGE(TAG, "Socket not initialized");
        return;
    }

    // Format data as string: tagnumber:timestamp:csi
    char payload[2048]; // Adjust size as needed
    int offset = snprintf(payload, sizeof(payload), "%d:%" PRIu64 ":", csi_data->tagnumber, csi_data->timestamp);

    // Convert CSI data to string
    for (int i = 0; i < csi_data->len; i++)
    {
        if (offset >= sizeof(payload))
        {
            ESP_LOGE(TAG, "Payload buffer overflow");
            return;
        }
        offset += snprintf(payload + offset, sizeof(payload) - offset, "%d,", csi_data->data[i]);
    }

    // Remove trailing comma
    if (offset > 0 && payload[offset - 1] == ',')
    {
        payload[offset - 1] = '\0';
    }

    ESP_LOGI(TAG, "Sending CSI data: %s", payload);

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(MONITOR_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(MONITOR_IP);

    int err = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
    }
    else
    {
        ESP_LOGI(TAG, "Sent CSI data to monitor");
    }
}

void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *csi_info)
{
    if (!csi_info)
    {
        ESP_LOGE(TAG, "CSI info is NULL");
        return;
    }

    ESP_LOGI(TAG, "CSI callback invoked, data length: %d, mac: " MACSTR ", RSSI: %d",
             csi_info->len, MAC2STR(csi_info->mac), csi_info->rx_ctrl.rssi);

    csi_data_t csi_data;
    memset(&csi_data, 0, sizeof(csi_data_t));

    csi_data.tagnumber = node_index;
    csi_data.timestamp = esp_timer_get_time(); // Get timestamp
    csi_data.len = csi_info->len;

    if (csi_data.len > CSI_DATA_LEN)
    {
        ESP_LOGE(TAG, "CSI data length too large: %d", csi_data.len);
        return;
    }

    // Copy CSI data (signed values)
    memcpy(csi_data.data, csi_info->buf, csi_data.len);

    if (xQueueSend(csi_data_queue, &csi_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to send CSI data to queue");
    }
    else
    {
        ESP_LOGI(TAG, "CSI data enqueued");
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "Received ESP-NOW message from " MACSTR " with length %d", MAC2STR(info->src_addr), len);

    // Ignore messages from self
    if (memcmp(info->src_addr, node_mac_addresses[node_index], 6) == 0)
    {
        ESP_LOGI(TAG, "Node %d received its own message, ignoring", node_index);
        return;
    }

    if (len != sizeof(uint8_t))
    {
        ESP_LOGE(TAG, "Unexpected ESP-NOW data length from " MACSTR ": %d", MAC2STR(info->src_addr), len);
        return;
    }

    uint8_t received_broadcaster = *data;

    if (received_broadcaster == 255)
    {
        // Disable CSI
        ESP_LOGI(TAG, "Node %d received disable CSI command from Node %d", node_index, received_broadcaster);
        ESP_ERROR_CHECK(esp_wifi_set_csi(false));
    }
    else if (received_broadcaster == node_index)
    {
        // This node is now the broadcaster
        current_broadcaster = node_index;
        ESP_LOGI(TAG, "Node %d is now the broadcaster", node_index);
    }
    else
    {
        // Another node is broadcasting, enable CSI
        current_broadcaster = received_broadcaster;
        ESP_LOGI(TAG, "Node %d enabling CSI to receive from Node %d", node_index, received_broadcaster);
        ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    }
}
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "ESP-NOW send callback, mac: " MACSTR ", status: %d", MAC2STR(mac_addr), status);
}

static void espnow_init()
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)"pmk1234567890123"));

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    for (int i = 0; i < MAX_NUM_NODES; ++i)
    {
        if (i == node_index)
            continue; // Skip adding self as peer
        memcpy(peer.peer_addr, node_mac_addresses[i], 6);
        peer.channel = 0; // Set to zero in Station Mode
        peer.ifidx = ESP_IF_WIFI_STA;
        peer.encrypt = false;
        esp_err_t add_peer_status = esp_now_add_peer(&peer);
        if (add_peer_status != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(add_peer_status));
        }
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    // Register send callback to handle send status
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    // Set MAC address based on tag number
    if (tag_number >= 0 && tag_number < MAX_NUM_NODES)
    {
        ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, node_mac_addresses[tag_number]));
        node_index = tag_number;
    }
    else
    {
        ESP_LOGE(TAG, "Invalid tag number!");
        return;
    }

    ESP_LOGI(TAG, "Assigned MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
             node_mac_addresses[tag_number][0], node_mac_addresses[tag_number][1],
             node_mac_addresses[tag_number][2], node_mac_addresses[tag_number][3],
             node_mac_addresses[tag_number][4], node_mac_addresses[tag_number][5]);

    espnow_init();

    // Configure CSI parameters and register the callback
    wifi_csi_config_t csi_config = {
        .lltf_en = true,            // Enable L-LTF
        .htltf_en = true,           // Enable HT-LTF
        .stbc_htltf2_en = true,     // Enable STBC HT-LTF2
        .ltf_merge_en = true,       // Enable LTF merging
        .channel_filter_en = false, // Disable channel filtering
        .manu_scale = false,        // Disable manual scaling
        .shift = 0};
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&wifi_csi_rx_cb, NULL));

    // Wait for IP address
    xEventGroupWaitBits(wifi_event_group, IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    udp_socket_init();

    // Create queue for CSI data
    csi_data_queue = xQueueCreate(10, sizeof(csi_data_t));
    if (csi_data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    // Initialize current_broadcaster
    if (node_index == 0)
    {
        current_broadcaster = 0; // Node 0 starts as broadcaster
    }
    else
    {
        current_broadcaster = 255; // 255 indicates no broadcaster
    }

    while (true)
    {
        if (current_broadcaster == node_index)
        {
            // Node is the broadcaster
            ESP_LOGI(TAG, "Node %d is broadcasting...", node_index);

            // Inform other nodes to enable CSI
            uint8_t broadcaster_id = node_index;
            for (int i = 0; i < MAX_NUM_NODES; ++i)
            {
                if (i == node_index)
                    continue;
                esp_err_t result = esp_now_send(node_mac_addresses[i], &broadcaster_id, sizeof(broadcaster_id));
                if (result == ESP_OK)
                {
                    ESP_LOGI(TAG, "Node %d informed Node %d to enable CSI", node_index, i);
                }
                else
                {
                    ESP_LOGE(TAG, "Error sending data to Node %d: %s", i, esp_err_to_name(result));
                }
            }

            // Allow time for nodes to enable CSI
            vTaskDelay(pdMS_TO_TICKS(100));

            // Send ESP-NOW data packets to other nodes
            uint32_t start_time = xTaskGetTickCount();
            uint8_t espnow_payload[10] = {0}; // Example payload
            while ((xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS < BROADCAST_INTERVAL)
            {
                for (int i = 0; i < MAX_NUM_NODES; ++i)
                {
                    if (i == node_index)
                        continue;
                    esp_err_t result = esp_now_send(node_mac_addresses[i], espnow_payload, sizeof(espnow_payload));
                    if (result != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Error sending ESP-NOW data to Node %d: %s", i, esp_err_to_name(result));
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(100)); // Adjust delay as needed
            }

            // Disable CSI on other nodes
            uint8_t disable_broadcast = 255;
            for (int i = 0; i < MAX_NUM_NODES; ++i)
            {
                if (i == node_index)
                    continue;
                esp_err_t result = esp_now_send(node_mac_addresses[i], &disable_broadcast, sizeof(disable_broadcast));
                if (result == ESP_OK)
                {
                    ESP_LOGI(TAG, "Node %d informed Node %d to disable CSI", node_index, i);
                }
                else
                {
                    ESP_LOGE(TAG, "Error sending disable to Node %d: %s", i, esp_err_to_name(result));
                }
            }

            // Pass the token to the next node
            uint8_t next_broadcaster = (node_index + 1) % MAX_NUM_NODES;
            esp_err_t result = esp_now_send(node_mac_addresses[next_broadcaster], &next_broadcaster, sizeof(next_broadcaster));
            if (result == ESP_OK)
            {
                ESP_LOGI(TAG, "Node %d passed token to Node %d", node_index, next_broadcaster);
            }
            else
            {
                ESP_LOGE(TAG, "Error passing token to Node %d: %s", next_broadcaster, esp_err_to_name(result));
            }

            // Update current_broadcaster to indicate no longer broadcasting
            current_broadcaster = 255;

            // Disable CSI on self
            ESP_ERROR_CHECK(esp_wifi_set_csi(false));
        }
        else
        {
            // Node is not the broadcaster, check if CSI data is available
            csi_data_t csi_data;
            while (xQueueReceive(csi_data_queue, &csi_data, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                ESP_LOGI(TAG, "Node %d sending CSI data", node_index);
                send_csi_data(&csi_data);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (sock != -1)
    {
        close(sock);
    }
}