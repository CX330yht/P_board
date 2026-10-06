#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdint.h>

typedef enum
{
    SOFT_I2C_OK = 0,
    SOFT_I2C_NACK,
    SOFT_I2C_TIMEOUT,
    SOFT_I2C_BUS_BUSY,
    SOFT_I2C_NOT_READY,
    SOFT_I2C_BAD_ARGUMENT
} SoftI2C_Status;

/* Uses PB9=SCL, PB8=SDA; both must be open drain with external pull-ups. */
void SoftI2C_Init(void);

/* BoardDelay_Init() must run first. Pass a 7-bit address, without shifting. */
SoftI2C_Status SoftI2C_Probe(uint8_t address7);
SoftI2C_Status SoftI2C_Write(uint8_t address7,
                           const uint8_t *data, uint16_t length);

#endif