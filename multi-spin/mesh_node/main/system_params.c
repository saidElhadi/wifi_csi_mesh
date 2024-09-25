#include "system_params.h"

// MAC addresses for nodes
const uint8_t node_mac_addresses[MAX_NUM_NODES][6] = {
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x00}, // Node 0
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x01}, // Node 1
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x02}, // Node 2
    {0x1a, 0x00, 0x00, 0x00, 0x00, 0x03}  // Node 3
};

// Channel sequence for broadcasting
const uint8_t channel_sequence[CHANNELS_NUMBER] = {1, 6, 11};

uint8_t current_broadcaster = 0; // Initialize with Node 0 as the first broadcaster
