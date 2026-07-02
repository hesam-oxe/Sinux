#pragma once
#include <stdint.h>

void fb_set_info(uint64_t addr, uint32_t pitch,
                 uint32_t width, uint32_t height, uint8_t bpp);

void     fb_init(void);
int      fb_available(void);

void     fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb);
void     fb_fill_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t rgb);
void     fb_clear(uint32_t rgb);

/* text */
void     fb_putc(char c);
void     fb_puts(const char *s);
void     fb_set_fg(uint32_t rgb);
void     fb_set_bg(uint32_t rgb);
uint32_t fb_get_fg(void);
uint32_t fb_get_bg(void);

/* cursor */
void     fb_cursor_show(void);
void     fb_cursor_hide(void);

/* colors */
#define FB_BLACK    0x000000u
#define FB_WHITE    0xFFFFFFu
#define FB_RED      0xFF4444u
#define FB_GREEN    0x44FF88u
#define FB_CYAN     0x44FFFFu
#define FB_YELLOW   0xFFFF44u
#define FB_LGRAY    0xAAAAAAu
#define FB_DGRAY    0x333333u
