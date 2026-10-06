#include "pboard_app.h"
#include "board_delay.h"
#include "oled_ssd1306.h"
#include "usart.h"

#define SAMPLE_PERIOD_MS      100U
#define DISPLAY_PERIOD_MS      25U
#define HEARTBEAT_PERIOD_MS  1000U
#define INA_RETRY_PERIOD_MS  1000U
#define OLED_RETRY_PERIOD_MS 1000U

volatile PBoardDebug g_pboard_debug;
volatile PBoardPowerState g_pboard_power;
volatile PBoardOLEDLink g_pboard_oled_link;

static uint32_t last_sample_ms;
static uint32_t last_display_ms;
static uint32_t last_heartbeat_ms;
static uint32_t last_ina_init_ms;
static uint32_t last_oled_init_ms;
static uint8_t next_oled_page;

static void TryInitINA226(void)
{
    last_ina_init_ms = HAL_GetTick();
    INA226_Status status = INA226_Init();

    g_pboard_power.last_status = status;
    g_pboard_power.valid = 0U;
    g_pboard_power.online = (status == INA226_OK) ? 1U : 0U;
    if (status != INA226_OK)
    {
        g_pboard_power.error_count++;
    }
}

static void UpdatePower(uint32_t now)
{
    if (g_pboard_power.online == 0U)
    {
        if ((uint32_t)(now - last_ina_init_ms) >= INA_RETRY_PERIOD_MS)
        {
            TryInitINA226();
        }
        return;
    }

    INA226_Data sample;
    INA226_Status status = INA226_ReadData(&sample);
    g_pboard_power.last_status = status;
    g_pboard_power.valid = 0U;

    if (status == INA226_OK)
    {
        g_pboard_power.data = sample;
        g_pboard_power.valid = 1U;
        g_pboard_power.sample_ok_count++;
    }
    else if (status != INA226_NOT_READY)
    {
        g_pboard_power.error_count++;
        if (status != INA226_OVERFLOW)
        {
            g_pboard_power.online = 0U;
            last_ina_init_ms = HAL_GetTick();
        }
    }
}

static void TryInitOLED(void)
{
    g_pboard_oled_link.probe_count++;
    g_pboard_oled_link.online = 0U;
    g_pboard_oled_link.initialized = 0U;
    g_pboard_oled_link.address7 = 0U;
    next_oled_page = 0U;

    uint8_t address7 = 0x3CU;
    SoftI2C_Status status = SoftI2C_Probe(address7);
    if (status == SOFT_I2C_NACK)
    {
        address7 = 0x3DU;
        status = SoftI2C_Probe(address7);
    }

    if (status == SOFT_I2C_OK)
    {
        g_pboard_oled_link.address7 = address7;
        status = OLED_Init(address7);
    }
    g_pboard_oled_link.last_status = status;
    if (status == SOFT_I2C_OK)
    {
        g_pboard_oled_link.online = 1U;
        g_pboard_oled_link.initialized = 1U;
        g_pboard_oled_link.init_ok_count++;

        OLED_DrawString(0U, 0U, "P BOARD");
        OLED_DrawString(0U, 2U, "SSD1306 128X64");
        OLED_DrawString(0U, 4U, "OLED TEST OK");
        OLED_DrawString(0U, 6U, "0123456789");
    }
    else
    {
        g_pboard_oled_link.error_count++;
    }
    /* Start the retry interval after the initialization attempt finishes. */
    last_oled_init_ms = HAL_GetTick();
}

static void UpdateOLED(uint32_t now)
{
    if (g_pboard_oled_link.initialized == 0U)
    {
        if ((uint32_t)(now - last_oled_init_ms) >= OLED_RETRY_PERIOD_MS)
        {
            TryInitOLED();
        }
        return;
    }

    SoftI2C_Status status = OLED_RefreshPage(next_oled_page);
    g_pboard_oled_link.last_status = status;
    if (status == SOFT_I2C_OK)
    {
        next_oled_page++;
        if (next_oled_page >= OLED_PAGE_COUNT)
        {
            next_oled_page = 0U;
            g_pboard_oled_link.frame_ok_count++;
        }
    }
    else
    {
        g_pboard_oled_link.online = 0U;
        g_pboard_oled_link.initialized = 0U;
        g_pboard_oled_link.error_count++;
        next_oled_page = 0U;
        last_oled_init_ms = HAL_GetTick();
    }
}

void PBoardApp_Init(void)
{
    BoardDelay_Init();
    SoftI2C_Init();
    g_pboard_debug = (PBoardDebug){0};
    g_pboard_power = (PBoardPowerState){0};
    g_pboard_oled_link = (PBoardOLEDLink){0};
    /* The four-pin module has no MCU-controlled reset pin. */
    HAL_Delay(100U);
    TryInitINA226();
    TryInitOLED();

    uint32_t now = HAL_GetTick();
    last_sample_ms = now;
    last_display_ms = now;
    last_heartbeat_ms = now;
    last_oled_init_ms = now;
}

void PBoardApp_Process(void)
{
    uint32_t now = HAL_GetTick();

    g_pboard_debug.uptime_ms = now;
    g_pboard_debug.loop_count++;

    if ((uint32_t)(now - last_sample_ms) >= SAMPLE_PERIOD_MS)
    {
        last_sample_ms = now;
        g_pboard_debug.sample_slot_count++;
        UpdatePower(now);
    }

    if ((uint32_t)(now - last_display_ms) >= DISPLAY_PERIOD_MS)
    {
        last_display_ms = now;
        g_pboard_debug.display_slot_count++;
        UpdateOLED(now);
    }

    if ((uint32_t)(now - last_heartbeat_ms) >= HEARTBEAT_PERIOD_MS)
    {
        static uint8_t message[] = "P_BOARD ALIVE\r\n";

        last_heartbeat_ms = now;
        g_pboard_debug.heartbeat_count++;
        if (HAL_UART_Transmit(&huart1, message,
                              (uint16_t)(sizeof(message) - 1U), 20U) == HAL_OK)
        {
            g_pboard_debug.uart_tx_ok_count++;
        }
        else
        {
            g_pboard_debug.uart_tx_error_count++;
        }
    }
}