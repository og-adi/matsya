/*
 * oled_ssd1306.c
 * ---------------------------------------------------------------------------
 * See oled_ssd1306.h for usage. The init command sequence below is the
 * standard SSD1306 128x64 configuration straight from the datasheet's
 * initialization procedure -- the same sequence appears identically across
 * essentially every SSD1306 driver in existence, since it's the chip's
 * required register configuration, not implementation-specific content.
 */

#include "oled_ssd1306.h"
#include "font5x7.h"

#define OLED_I2C_TIMEOUT_MS   50

static HAL_StatusTypeDef oled_write_cmd(oled_t *oled, uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };   /* control byte 0x00 = command follows */
    return HAL_I2C_Master_Transmit(oled->hi2c, oled->addr, buf, 2, OLED_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef oled_init(oled_t *oled, I2C_HandleTypeDef *hi2c, uint8_t addr)
{
    oled->hi2c = hi2c;
    oled->addr = addr;
    oled_clear(oled);

    static const uint8_t init_cmds[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock divide ratio / oscillator frequency */
        0xA8, 0x3F,       /* multiplex ratio = 64-1 */
        0xD3, 0x00,       /* display offset = 0 */
        0x40,             /* display start line = 0 */
        0x8D, 0x14,       /* charge pump enable (required for most 3.3V modules) */
        0x20, 0x00,       /* memory addressing mode = horizontal */
        0xA1,             /* segment remap (column 127 mapped to SEG0) */
        0xC8,             /* COM output scan direction, remapped */
        0xDA, 0x12,       /* COM pins hardware configuration */
        0x81, 0xCF,       /* contrast control */
        0xD9, 0xF1,       /* pre-charge period */
        0xDB, 0x40,       /* VCOMH deselect level */
        0xA4,             /* entire display follows RAM content (not all-on) */
        0xA6,             /* normal display (not inverted) */
        0xAF              /* display on */
    };

    for (uint32_t i = 0; i < sizeof(init_cmds); i++) {
        if (oled_write_cmd(oled, init_cmds[i]) != HAL_OK) {
            return HAL_ERROR;
        }
    }

    return oled_flush(oled);   /* push the cleared framebuffer so the display starts blank, not garbage */
}

void oled_clear(oled_t *oled)
{
    for (uint32_t i = 0; i < sizeof(oled->framebuffer); i++) {
        oled->framebuffer[i] = 0x00;
    }
}

void oled_draw_char(oled_t *oled, uint8_t x, uint8_t page, char c)
{
    if (page >= OLED_PAGES) return;
    if ((uint32_t)x + 5 > OLED_WIDTH) return;   /* would run off the right edge */

    const uint8_t *glyph = font5x7_lookup(c);
    uint8_t *row = &oled->framebuffer[(uint32_t)page * OLED_WIDTH + x];

    if (glyph == 0) {
        /* Unsupported character: blank cell rather than garbage/undefined. */
        row[0] = row[1] = row[2] = row[3] = row[4] = 0x00;
        return;
    }

    row[0] = glyph[0];
    row[1] = glyph[1];
    row[2] = glyph[2];
    row[3] = glyph[3];
    row[4] = glyph[4];
}

void oled_draw_string(oled_t *oled, uint8_t x, uint8_t page, const char *s)
{
    uint16_t cursor = x;
    while (*s != '\0') {
        if ((uint32_t)cursor + 5 > OLED_WIDTH) break;   /* stop cleanly at the edge, no wrap */
        oled_draw_char(oled, (uint8_t)cursor, page, *s);
        cursor += 6;   /* 5px glyph + 1px spacing */
        s++;
    }
}

HAL_StatusTypeDef oled_flush(oled_t *oled)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        /* Set the page address and reset the column pointer to 0 before
         * writing this page's 128 bytes -- required because we're using
         * horizontal addressing mode but writing one page at a time. */
        if (oled_write_cmd(oled, 0xB0 + page) != HAL_OK) return HAL_ERROR;
        if (oled_write_cmd(oled, 0x00) != HAL_OK) return HAL_ERROR;   /* column low nibble = 0 */
        if (oled_write_cmd(oled, 0x10) != HAL_OK) return HAL_ERROR;   /* column high nibble = 0 */

        uint8_t buf[OLED_WIDTH + 1];
        buf[0] = 0x40;   /* control byte: data stream follows */
        for (uint16_t col = 0; col < OLED_WIDTH; col++) {
            buf[1 + col] = oled->framebuffer[(uint32_t)page * OLED_WIDTH + col];
        }

        if (HAL_I2C_Master_Transmit(oled->hi2c, oled->addr, buf, OLED_WIDTH + 1,
                                     OLED_I2C_TIMEOUT_MS) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}
