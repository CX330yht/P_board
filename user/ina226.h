#ifndef INA226_H
#define INA226_H

#include <stdint.h>

typedef enum
{
    INA226_OK = 0,
    INA226_NOT_READY,
    INA226_I2C_ERROR,
    INA226_ID_ERROR,
    INA226_CONFIG_ERROR,
    INA226_OVERFLOW,
    INA226_BAD_ARGUMENT
} INA226_Status;

typedef struct
{
    float voltage_v;
    float current_a;
    float power_w;
    uint32_t sample_tick_ms;
} INA226_Data;

INA226_Status INA226_Init(void);
INA226_Status INA226_ReadData(INA226_Data *data);

#endif