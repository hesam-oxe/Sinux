#pragma once
#include <stdint.h>

/*
 * pci_find_vga_bar0 — دنبال اولین دستگاه PCI class=DISPLAY (0x03)
 * می‌گرده و آدرس فیزیکی BAR0 (framebuffer) رو برمی‌گردونه.
 *
 * چرا لازمه؟ آدرس فریم‌بافر VGA/BGA ثابت نیست — بسته به نسخهٔ
 * QEMU، machine type (pc/q35)، و ترتیب دستگاه‌ها عوض میشه.
 * تنها راه مطمئن خوندن واقعی از PCI config space هست.
 *
 * برمی‌گردونه: آدرس فیزیکی BAR0، یا 0 اگه چیزی پیدا نشد.
 */
uint64_t pci_find_vga_bar0(void);
