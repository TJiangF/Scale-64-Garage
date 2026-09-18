/*
 * lcd_font.h - tiny 5x7 bitmap font renderer for a RGB565 framebuffer.
 *
 * Kept independent from esp_lcd so the same buffer can be pushed with
 * esp_lcd_panel_draw_bitmap() or validated on the host.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_FONT_W 5
#define LCD_FONT_H 7

/* Fill a rectangle of the framebuffer with a colour. */
void lcd_font_fill_rect(uint16_t *fb, int fb_w, int fb_h,
                        int x, int y, int w, int h, uint16_t color);

/*
 * Draw an ASCII string into the framebuffer.
 * Each glyph cell is cleared with `bg` first, then the pixels are painted
 * with `fg`, scaled by `scale` (1 = 5x7 pixels).
 * Returns the x coordinate just after the last glyph.
 */
int lcd_font_draw_text(uint16_t *fb, int fb_w, int fb_h, int x, int y,
                       const char *text, uint16_t fg, uint16_t bg, int scale);

#ifdef __cplusplus
}
#endif
