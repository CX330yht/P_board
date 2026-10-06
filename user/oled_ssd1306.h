#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include <stdint.h>
#include "soft_i2c.h"

#define OLED_WIDTH       128U
#define OLED_PAGE_COUNT    8U

/* Configure SSD1306, clear all pages, then enable the display. */
SoftI2C_Status OLED_Init(uint8_t address7);

/* These functions change the RAM buffer, not the screen immediately. */
void OLED_Clear(void);
void OLED_DrawString(uint8_t column, uint8_t page, const char *text);

/* Send one eight-pixel-high page from the RAM buffer. */
SoftI2C_Status OLED_RefreshPage(uint8_t page);

#endif