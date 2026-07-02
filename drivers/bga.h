#pragma once
#include <stdint.h>

/* Bochs VBE / BGA — works in QEMU out of the box */

/*
 * فریم‌بافر فیزیکی VGA استاندارد QEMU.
 * فقط با -vga std (یا -device VGA) درست کار می‌کنه، نه bochs-display
 * (اون یه دستگاه PCI-only هست و اصلاً به این آدرس ثابت گوش نمی‌ده).
 */
#define BGA_ADDR  0xE0000000u

int  bga_init(uint16_t width, uint16_t height, uint16_t bpp);
int  bga_available(void);       /* 1 if BGA was found and inited */
uint64_t bga_fb_phys_addr(void); /* actual physical addr found via PCI */
