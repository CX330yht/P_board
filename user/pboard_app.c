#include "pboard_app.h"
#include "board_delay.h"
#include "oled_ssd1306.h"
#include "usart.h"

#define SAMPLE_PERIOD_MS      100U
#define DISPLAY_PERIOD_MS      25U
#define HEARTBEAT_PERIOD_MS  1000U
#define INA_RETRY_PERIOD_MS  1000U
#define OLED_RETRY_PERIOD_MS 1000U
#define POWER_STALE_MS        500U

volatile PBoardDebug g_pboard_debug;
volatile PBoardPowerState g_pboard_power;
volatile PBoardOLEDLink g_pboard_oled_link;

static uint32_t last_sample_ms;
static uint32_t last_display_ms;
static uint32_t last_heartbeat_ms;
static uint32_t last_ina_init_ms;
static uint32_t last_oled_init_ms;
static uint8_t next_oled_page;

static uint8_t ValueFitsDisplay(float value)
{
    /* These comparisons also reject NaN and infinity. */
    return (value >= -99999.0f && value <= 99999.0f) ? 1U : 0U;
}

static void DrawValueLine(uint8_t page, char label, char unit, float value)
{
    /* RenderPowerPage validates the range before calling this function. */
    float magnitude = (value < 0.0f) ? -value : value;
    uint32_t milli = (uint32_t)(magnitude * 1000.0f + 0.5f);
    uint32_t whole = milli / 1000U;
    uint32_t fraction = milli % 1000U;
    char digits[10];
    char line[22];
    uint8_t digit_count = 0U;
    uint8_t position = 0U;

    line[position++] = label;
    line[position++] = ':';
    line[position++] = ' ';
    if (value < 0.0f && milli != 0U)
    {
        line[position++] = '-';
    }

    /* Extract whole-number digits backwards, then copy them forwards. */
    do
    {
        digits[digit_count++] = (char)('0' + whole % 10U);
        whole /= 10U;
    } while (whole != 0U);
    while (digit_count != 0U)
    {
        line[position++] = digits[--digit_count];
    }

    line[position++] = '.';
    line[position++] = (char)('0' + fraction / 100U);
    line[position++] = (char)('0' + (fraction / 10U) % 10U);
    line[position++] = (char)('0' + fraction % 10U);
    line[position++] = ' ';
    line[position++] = unit;
    line[position] = '\0';

    OLED_DrawString(0U, page, line);
}

static void RenderPowerPage(void)
{
    /* Only the main loop writes this state; one copy supplies the whole frame. */
    PBoardPowerState power = g_pboard_power;
    /* Read time after the snapshot, not the earlier Process() timestamp. */
    uint32_t now = HAL_GetTick();
    const char *state_text = "POWER OK";
    uint8_t show_values = 0U;

    if (power.last_status == INA226_OVERFLOW)
    {
        state_text = "INA OVERFLOW";
    }
    else if (power.online == 0U)
    {
        state_text = "INA OFFLINE";
    }
    else if (power.valid == 0U)
    {
        state_text = "WAIT SAMPLE";
    }
    else if ((uint32_t)(now - power.data.sample_tick_ms) >= POWER_STALE_MS)
    {
        state_text = "SAMPLE STALE";
    }
    else if (ValueFitsDisplay(power.data.voltage_v) == 0U ||
             ValueFitsDisplay(power.data.current_a) == 0U ||
             ValueFitsDisplay(power.data.power_w) == 0U)
    {
        state_text = "DATA INVALID";
    }
    else
    {
        show_values = 1U;
    }

    OLED_Clear();
    OLED_DrawString(0U, 0U, state_text);
    if (show_values != 0U)
    {
        DrawValueLine(2U, 'V', 'V', power.data.voltage_v);
        DrawValueLine(4U, 'I', 'A', power.data.current_a);
        DrawValueLine(6U, 'P', 'W', power.data.power_w);
    }
    else
    {
        OLED_DrawString(0U, 2U, "V: ---.--- V");
        OLED_DrawString(0U, 4U, "I: ---.--- A");
        OLED_DrawString(0U, 6U, "P: ---.--- W");
    }
}

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

        RenderPowerPage();
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

    if (next_oled_page == 0U)
    {
        RenderPowerPage();
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