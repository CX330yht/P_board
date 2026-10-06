#ifndef PBOARD_APP_H
#define PBOARD_APP_H

#include <stdint.h>
#include "ina226.h"
#include "soft_i2c.h"

typedef struct
{
    uint32_t uptime_ms;
    uint32_t loop_count;
    uint32_t sample_slot_count;
    uint32_t display_slot_count;
    uint32_t heartbeat_count;
    uint32_t uart_tx_ok_count;
    uint32_t uart_tx_error_count;
} PBoardDebug;

typedef struct
{
    uint8_t online;
    uint8_t valid;
    INA226_Status last_status;
    INA226_Data data;
    uint32_t sample_ok_count;
    uint32_t error_count;
} PBoardPowerState;

typedef struct
{
    uint8_t online;
    uint8_t address7;
    uint8_t initialized;
    SoftI2C_Status last_status;
    uint32_t probe_count;
    uint32_t init_ok_count;
    uint32_t frame_ok_count;
    uint32_t error_count;
} PBoardOLEDLink;

extern volatile PBoardDebug g_pboard_debug;
extern volatile PBoardPowerState g_pboard_power;
extern volatile PBoardOLEDLink g_pboard_oled_link;

void PBoardApp_Init(void);
void PBoardApp_Process(void);

#endif