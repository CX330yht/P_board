#include "board_delay.h"
#include "tim.h"

void BoardDelay_Init(void)
{
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
    {
        Error_Handler();
    }
}

void BoardDelay_Us(uint16_t us)
{
    uint16_t start = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);

    /* TIM2 is a 16-bit counter; this subtraction also handles overflow. */
    while ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim2) - start) < us)
    {
    }
}