#include "pci.h"
#include "../lib/io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* class code 0x03 = DISPLAY_CONTROLLER (VGA و همه‌ی زیرکلاس‌هاش) */
#define PCI_CLASS_DISPLAY 0x03

static uint32_t
pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t address =
        (1u << 31) |
        ((uint32_t)bus  << 16) |
        ((uint32_t)dev  << 11) |
        ((uint32_t)func << 8)  |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

static void
pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func,
                    uint8_t offset, uint32_t value)
{
    uint32_t address =
        (1u << 31) |
        ((uint32_t)bus  << 16) |
        ((uint32_t)dev  << 11) |
        ((uint32_t)func << 8)  |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

/*
 * BAR0 رو می‌خونه و سایز/مموری بودنش رو تشخیص میده.
 * برای BARهای memory-mapped 32-بیتی، بیت‌های پایین باید صفر بشن
 * چون فلگ‌های type رو نگه می‌دارن (bit0 = memory/io, bit1-2 = type).
 */
static uint64_t
pci_read_bar0(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t bar0 = pci_config_read32(bus, dev, func, 0x10);

    /* bit0 == 1 یعنی I/O space، نه memory — برای فریم‌بافر نمی‌خوایمش */
    if (bar0 & 0x1)
        return 0;

    /* بیت‌های 1-2: نوع آدرس (00=32bit, 10=64bit) */
    uint32_t type = (bar0 >> 1) & 0x3;

    uint64_t addr = bar0 & 0xFFFFFFF0u;

    if (type == 0x2) {
        /* 64-bit BAR — نیمهٔ بالا توی BAR بعدی (offset+4) هست */
        uint32_t bar1 = pci_config_read32(bus, dev, func, 0x14);
        addr |= ((uint64_t)bar1 << 32);
    }

    return addr;
}

uint64_t
pci_find_vga_bar0(void)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_config_read32((uint8_t)bus, dev, 0, 0x00);
            uint16_t vendor = id & 0xFFFF;
            if (vendor == 0xFFFF) continue;   /* دستگاه‌ای نیست */

            uint32_t class_reg = pci_config_read32((uint8_t)bus, dev, 0, 0x08);
            uint8_t class_code = (class_reg >> 24) & 0xFF;

            if (class_code == PCI_CLASS_DISPLAY) {
                uint64_t bar0 = pci_read_bar0((uint8_t)bus, dev, 0);
                if (bar0) return bar0;
            }
        }
    }
    return 0;
}
