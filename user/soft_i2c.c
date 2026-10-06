#include "soft_i2c.h"
#include "board_delay.h"
#include "main.h"
#include "tim.h"

#define I2C_DELAY_US          5U
#define I2C_SCL_TIMEOUT_US 1000U

static void SetSCL(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, state);
}

static void SetSDA(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, state);
}

static void ReleaseBus(void)
{
    /* SET releases an open-drain line; the pull-up supplies the high level. */
    SetSDA(GPIO_PIN_SET);
    SetSCL(GPIO_PIN_SET);
}

static SoftI2C_Status RaiseSCL(void)
{
    uint16_t start = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
    SetSCL(GPIO_PIN_SET);

    while (HAL_GPIO_ReadPin(OLED_SCL_GPIO_Port, OLED_SCL_Pin) == GPIO_PIN_RESET)
    {
        if ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim2) - start) >= I2C_SCL_TIMEOUT_US)
        {
            return SOFT_I2C_TIMEOUT;
        }
    }
    return SOFT_I2C_OK;
}

static SoftI2C_Status Start(void)
{
    ReleaseBus();
    SoftI2C_Status status = RaiseSCL();
    if (status != SOFT_I2C_OK)
    {
        return status;
    }
    BoardDelay_Us(I2C_DELAY_US);

    if (HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin) == GPIO_PIN_RESET)
    {
        return SOFT_I2C_BUS_BUSY;
    }

    /* START: SDA goes low while SCL is high. */
    SetSDA(GPIO_PIN_RESET);
    BoardDelay_Us(I2C_DELAY_US);
    SetSCL(GPIO_PIN_RESET);
    return SOFT_I2C_OK;
}

static SoftI2C_Status Stop(void)
{
    SetSCL(GPIO_PIN_RESET);
    SetSDA(GPIO_PIN_RESET);
    BoardDelay_Us(I2C_DELAY_US);

    SoftI2C_Status status = RaiseSCL();
    if (status != SOFT_I2C_OK)
    {
        ReleaseBus();
        return status;
    }
    BoardDelay_Us(I2C_DELAY_US);

    /* STOP: SDA is released while SCL is high. */
    SetSDA(GPIO_PIN_SET);
    BoardDelay_Us(I2C_DELAY_US);
    if (HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin) == GPIO_PIN_RESET)
    {
        return SOFT_I2C_BUS_BUSY;
    }
    return SOFT_I2C_OK;
}

static SoftI2C_Status WriteByte(uint8_t value)
{
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
        SetSDA((value & 0x80U) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
        BoardDelay_Us(I2C_DELAY_US);

        SoftI2C_Status status = RaiseSCL();
        if (status != SOFT_I2C_OK)
        {
            return status;
        }
        BoardDelay_Us(I2C_DELAY_US);
        SetSCL(GPIO_PIN_RESET);
        value = (uint8_t)(value << 1U);
    }

    /* Ninth clock: release SDA so the target can acknowledge. */
    SetSDA(GPIO_PIN_SET);
    BoardDelay_Us(I2C_DELAY_US);
    SoftI2C_Status status = RaiseSCL();
    if (status != SOFT_I2C_OK)
    {
        return status;
    }
    BoardDelay_Us(I2C_DELAY_US);

    GPIO_PinState ack = HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin);
    SetSCL(GPIO_PIN_RESET);
    return (ack == GPIO_PIN_RESET) ? SOFT_I2C_OK : SOFT_I2C_NACK;
}

void SoftI2C_Init(void)
{
    ReleaseBus();
}

SoftI2C_Status SoftI2C_Write(uint8_t address7,
                           const uint8_t *data, uint16_t length)
{
    if (address7 > 0x7FU || (data == NULL && length != 0U))
    {
        return SOFT_I2C_BAD_ARGUMENT;
    }
    if (htim2.Instance == NULL || (htim2.Instance->CR1 & TIM_CR1_CEN) == 0U)
    {
        return SOFT_I2C_NOT_READY;
    }

    SoftI2C_Status status = Start();
    if (status != SOFT_I2C_OK)
    {
        ReleaseBus();
        return status;
    }

    /* The driver adds the write bit; callers always use the 7-bit address. */
    status = WriteByte((uint8_t)(address7 << 1U));
    for (uint16_t i = 0U; i < length && status == SOFT_I2C_OK; i++)
    {
        status = WriteByte(data[i]);
    }

    /* Finish even after NACK; a STOP failure takes precedence. */
    SoftI2C_Status stop_status = Stop();
    return (stop_status == SOFT_I2C_OK) ? status : stop_status;
}

SoftI2C_Status SoftI2C_Probe(uint8_t address7)
{
    return SoftI2C_Write(address7, NULL, 0U);
}