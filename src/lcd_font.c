#include "lcd_font.h"

#include <stddef.h>

/*
 * Each glyph is 7 rows of 5 bits, bit 4 (0x10) is the left most column.
 * The binary literals make the shapes readable directly in the source.
 */
typedef struct {
    char    ch;
    uint8_t rows[LCD_FONT_H];
} lcd_glyph_t;

static const lcd_glyph_t s_font[] = {
    { ' ', { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 } },
    { '-', { 0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000 } },
    { ':', { 0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000 } },
    { '0', { 0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110 } },
    { '1', { 0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 } },
    { '2', { 0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111 } },
    { '3', { 0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110 } },
    { '4', { 0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010 } },
    { '5', { 0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110 } },
    { '6', { 0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110 } },
    { '7', { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000 } },
    { '8', { 0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110 } },
    { '9', { 0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100 } },
    { 'A', { 0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 } },
    { 'C', { 0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110 } },
    { 'D', { 0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100 } },
    { 'E', { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111 } },
    { 'F', { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000 } },
    { 'G', { 0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111 } },
    { 'I', { 0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 } },
    { 'L', { 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111 } },
    { 'P', { 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000 } },
    { 'R', { 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 } },
    { 'S', { 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 } },
    { 'T', { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100 } },
    { 'V', { 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100 } },
    { 'W', { 0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001 } },
};

static const lcd_glyph_t *glyph_lookup(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].ch == c) {
            return &s_font[i];
        }
    }
    return &s_font[0]; /* unknown -> blank */
}

void lcd_font_fill_rect(uint16_t *fb, int fb_w, int fb_h,
                        int x, int y, int w, int h, uint16_t color)
{
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= fb_h) {
            continue;
        }
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= fb_w) {
                continue;
            }
            fb[py * fb_w + px] = color;
        }
    }
}

int lcd_font_draw_text(uint16_t *fb, int fb_w, int fb_h, int x, int y,
                       const char *text, uint16_t fg, uint16_t bg, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    int cursor = x;
    for (const char *p = text; *p != '\0'; p++) {
        const lcd_glyph_t *g = glyph_lookup(*p);

        /* clear the cell, including the 1px spacing column */
        lcd_font_fill_rect(fb, fb_w, fb_h, cursor, y,
                           (LCD_FONT_W + 1) * scale, LCD_FONT_H * scale, bg);

        for (int row = 0; row < LCD_FONT_H; row++) {
            uint8_t bits = g->rows[row];
            for (int col = 0; col < LCD_FONT_W; col++) {
                if (bits & (1u << (LCD_FONT_W - 1 - col))) {
                    lcd_font_fill_rect(fb, fb_w, fb_h,
                                       cursor + col * scale, y + row * scale,
                                       scale, scale, fg);
                }
            }
        }

        cursor += (LCD_FONT_W + 1) * scale;
    }
    return cursor;
}
