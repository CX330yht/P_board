#include "oled_ssd1306.h"
#include <stddef.h>
#include <string.h>

#define OLED_CHAR_WIDTH 6U

typedef struct
{
    char character;
    uint8_t columns[5];
} FontGlyph;

/* Five columns plus one blank column; each byte holds eight vertical pixels. */
static const FontGlyph font[] =
{
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x41, 0x49, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x4F, 0x49, 0x49, 0x49, 0x31}},
    {'6', {0x3E, 0x49, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x49, 0x3E}},
    {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x41}},
    {'D', {0x7F, 0x41, 0x41, 0x41, 0x3E}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x39}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x04, 0x02, 0x7F}},
    {'N', {0x7F, 0x06, 0x08, 0x30, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x7F, 0x20, 0x10, 0x20, 0x7F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x00, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x00, 0x36, 0x00, 0x00}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {'/', {0x40, 0x30, 0x08, 0x06, 0x01}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
};

static uint8_t framebuffer[OLED_PAGE_COUNT][OLED_WIDTH];
static uint8_t oled_address7;
static uint8_t oled_initialized;

static const uint8_t *FindGlyph(char character)
{
    if (character >= 'a' && character <= 'z')
    {
        character = (char)(character - 'a' + 'A');
    }
    for (size_t i = 0U; i < sizeof(font) / sizeof(font[0]); i++)
    {
        if (font[i].character == character)
        {
            return font[i].columns;
        }
    }
    /* The last entry is the fallback question mark. */
    return font[sizeof(font) / sizeof(font[0]) - 1U].columns;
}

void OLED_Clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

void OLED_DrawString(uint8_t column, uint8_t page, const char *text)
{
    if (text == NULL || page >= OLED_PAGE_COUNT || column >= OLED_WIDTH)
    {
        return;
    }
    while (*text != '\0')
    {
        if ((uint16_t)column + OLED_CHAR_WIDTH > OLED_WIDTH)
        {
            break;
        }
        const uint8_t *glyph = FindGlyph(*text);
        memcpy(&framebuffer[page][column], glyph, 5U);
        framebuffer[page][column + 5U] = 0U;
        column = (uint8_t)(column + OLED_CHAR_WIDTH);
        text++;
    }
}

SoftI2C_Status OLED_RefreshPage(uint8_t page)
{
    if (page >= OLED_PAGE_COUNT)
    {
        return SOFT_I2C_BAD_ARGUMENT;
    }
    if (oled_initialized == 0U)
    {
        return SOFT_I2C_NOT_READY;
    }

    /* Control=0x00: commands; select page and reset its column to zero. */
    uint8_t commands[] = {0x00U, (uint8_t)(0xB0U + page), 0x00U, 0x10U};
    SoftI2C_Status status = SoftI2C_Write(oled_address7, commands, sizeof(commands));
    if (status != SOFT_I2C_OK)
    {
        oled_initialized = 0U;
        return status;
    }

    /* Control=0x40: pixel data; one transaction writes exactly one page. */
    uint8_t packet[OLED_WIDTH + 1U];
    packet[0] = 0x40U;
    memcpy(&packet[1], framebuffer[page], OLED_WIDTH);
    status = SoftI2C_Write(oled_address7, packet, sizeof(packet));
    if (status != SOFT_I2C_OK)
    {
        oled_initialized = 0U;
    }
    return status;
}

SoftI2C_Status OLED_Init(uint8_t address7)
{
    oled_initialized = 0U;
    if (address7 > 0x7FU)
    {
        return SOFT_I2C_BAD_ARGUMENT;
    }
    oled_address7 = address7;

    static const uint8_t init_commands[] =
    {
        0x00U,              /* I2C control byte: following bytes are commands. */
        0xAEU,              /* Display off during configuration. */
        0x2EU,              /* Disable scrolling. */
        0xD5U, 0x80U,       /* Clock divide ratio and oscillator. */
        0xA8U, 0x3FU,       /* 64 multiplexed rows. */
        0xD3U, 0x00U,       /* Display offset zero. */
        0x40U,              /* Display start line zero. */
        0x8DU, 0x14U,       /* Enable internal charge pump. */
        0x20U, 0x02U,       /* Page addressing mode. */
        0xA1U,              /* Segment remap. */
        0xC8U,              /* COM scan remap. */
        0xDAU, 0x12U,       /* COM layout for this 128x64 module. */
        0x81U, 0x7FU,       /* Contrast. */
        0xD9U, 0xF1U,       /* Pre-charge periods. */
        0xDBU, 0x20U,       /* VCOMH deselect level. */
        0xA4U,              /* Display follows RAM contents. */
        0xA6U               /* Normal display, no inversion. */
    };
    SoftI2C_Status status = SoftI2C_Write(oled_address7, init_commands,
                                        sizeof(init_commands));
    if (status != SOFT_I2C_OK)
    {
        return status;
    }

    OLED_Clear();
    oled_initialized = 1U;
    /* One-time clear before enabling display; steady-state refresh is split. */
    for (uint8_t page = 0U; page < OLED_PAGE_COUNT; page++)
    {
        status = OLED_RefreshPage(page);
        if (status != SOFT_I2C_OK)
        {
            return status;
        }
    }

    static const uint8_t display_on[] = {0x00U, 0xAFU};
    status = SoftI2C_Write(oled_address7, display_on, sizeof(display_on));
    if (status != SOFT_I2C_OK)
    {
        oled_initialized = 0U;
    }
    return status;
}