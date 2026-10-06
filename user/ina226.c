#include "ina226.h"
#include "i2c.h"

/* Board shunt: 0.001 ohm; Current_LSB: 0.001 A/bit.
 * CAL = 0.00512 / (0.001 * 0.001) = 5120 = 0x1400.
 */
#define INA226_ADDRESS        (0x40U << 1)
#define INA226_TIMEOUT_MS      10U
#define INA226_CONFIG_VALUE    0x4527U
#define INA226_CAL_VALUE       0x1400U

#define REG_CONFIG             0x00U
#define REG_BUS_VOLTAGE        0x02U
#define REG_CURRENT            0x04U
#define REG_CALIBRATION        0x05U
#define REG_MASK_ENABLE        0x06U
#define REG_MANUFACTURER_ID    0xFEU
#define REG_DIE_ID             0xFFU

#define MASK_CONVERSION_READY  (1U << 3)
#define MASK_MATH_OVERFLOW     (1U << 2)

static HAL_StatusTypeDef ReadRegister(uint8_t reg, uint16_t *value)
{
    uint8_t bytes[2];
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        &hi2c1, INA226_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        bytes, 2U, INA226_TIMEOUT_MS);

    if (status == HAL_OK)
    {
        *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    }
    return status;
}

static HAL_StatusTypeDef WriteRegister(uint8_t reg, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return HAL_I2C_Mem_Write(
        &hi2c1, INA226_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        bytes, 2U, INA226_TIMEOUT_MS);
}

static INA226_Status CheckConfiguration(void)
{
    uint16_t config;
    uint16_t calibration;

    if (ReadRegister(REG_CONFIG, &config) != HAL_OK ||
        ReadRegister(REG_CALIBRATION, &calibration) != HAL_OK)
    {
        return INA226_I2C_ERROR;
    }
    if (config != INA226_CONFIG_VALUE || calibration != INA226_CAL_VALUE)
    {
        return INA226_CONFIG_ERROR;
    }
    return INA226_OK;
}

INA226_Status INA226_Init(void)
{
    uint16_t manufacturer;
    uint16_t die;

    if (HAL_I2C_IsDeviceReady(&hi2c1, INA226_ADDRESS, 1U,
                              INA226_TIMEOUT_MS) != HAL_OK)
    {
        return INA226_I2C_ERROR;
    }
    if (ReadRegister(REG_MANUFACTURER_ID, &manufacturer) != HAL_OK ||
        ReadRegister(REG_DIE_ID, &die) != HAL_OK)
    {
        return INA226_I2C_ERROR;
    }
    if (manufacturer != 0x5449U || (die & 0xFFF0U) != 0x2260U)
    {
        return INA226_ID_ERROR;
    }

    /* 16 averages; bus/shunt conversion 1.1 ms each; continuous mode. */
    if (WriteRegister(REG_CALIBRATION, INA226_CAL_VALUE) != HAL_OK ||
        WriteRegister(REG_CONFIG, INA226_CONFIG_VALUE) != HAL_OK)
    {
        return INA226_I2C_ERROR;
    }
    return CheckConfiguration();
}

INA226_Status INA226_ReadData(INA226_Data *data)
{
    uint16_t flags;
    uint16_t bus_raw;
    uint16_t current_raw;

    if (data == 0)
    {
        return INA226_BAD_ARGUMENT;
    }

    INA226_Status status = CheckConfiguration();
    if (status != INA226_OK)
    {
        return status;
    }
    if (ReadRegister(REG_MASK_ENABLE, &flags) != HAL_OK)
    {
        return INA226_I2C_ERROR;
    }
    if ((flags & MASK_MATH_OVERFLOW) != 0U)
    {
        return INA226_OVERFLOW;
    }
    if ((flags & MASK_CONVERSION_READY) == 0U)
    {
        return INA226_NOT_READY;
    }
    if (ReadRegister(REG_BUS_VOLTAGE, &bus_raw) != HAL_OK ||
        ReadRegister(REG_CURRENT, &current_raw) != HAL_OK)
    {
        return INA226_I2C_ERROR;
    }

    INA226_Data next = {0};
    next.voltage_v = (float)bus_raw * 0.00125f;
    next.current_a = (float)(int16_t)current_raw * 0.001f;
    next.power_w = next.voltage_v * next.current_a;
    next.sample_tick_ms = HAL_GetTick();
    *data = next;
    return INA226_OK;
}