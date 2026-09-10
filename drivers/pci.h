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

/* ── generic helpers (used by virtio-blk and future drivers) ── */

/* Find a device by vendor:device IDs.  Returns 0 and fills in the
 * BDF triple on success, -1 when absent.  Function 0 only. */
int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *bus, uint8_t *dev, uint8_t *func);

/* Read BAR `bar` (0-5).  For I/O BARs bit0 is set and the address
 * is in the upper bits; for 32-bit memory BARs the low nibble holds
 * flags; 64-bit memory BARs combine the next BAR. */
uint64_t pci_read_bar(uint8_t bus, uint8_t dev, uint8_t func, int bar);

/* OR `bits` into the PCI command register (offset 0x04). */
void pci_write_cmd(uint8_t bus, uint8_t dev, uint8_t func, uint16_t bits);
