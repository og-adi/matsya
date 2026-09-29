/*
 * oled_ssd1306.h
 * ---------------------------------------------------------------------------
 * Minimal SSD1306 (128x64, I2C) driver – single display on I2C1.
 */

#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)

#define OLED_ADDR       (0x3C << 1)   /* default address, pre‑shifted for HAL */

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t addr;
    uint8_t framebuffer[OLED_PAGES * OLED_WIDTH];
} oled_t;

HAL_StatusTypeDef oled_init(oled_t *oled, I2C_HandleTypeDef *hi2c, uint8_t addr);
void oled_clear(oled_t *oled);
void oled_draw_char(oled_t *oled, uint8_t x, uint8_t page, char c);
void oled_draw_string(oled_t *oled, uint8_t x, uint8_t page, const char *s);
HAL_StatusTypeDef oled_flush(oled_t *oled);

#endif
