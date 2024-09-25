#ifndef SYSTEM_PARAMS_H
#define SYSTEM_PARAMS_H

#include <stdint.h>

#define MAX_NUM_NODES 3
#define CHANNELS_NUMBER 1
#define SLOT_LENGTH 1000  // 100 ms for example

extern uint8_t current_broadcaster;
extern const uint8_t node_mac_addresses[MAX_NUM_NODES][6];
extern const uint8_t channel_sequence[CHANNELS_NUMBER];

#endif