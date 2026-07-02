#include "bga.h"
#include "fb.h"
#include "pci.h"
#include "../lib/io.h"
#include <stdint.h>

/* ── BGA I/O ports ──────────────────────────────────────── */
#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

#define VBE_DISPI_INDEX_ID      0
#define VBE_DISPI_INDEX_XRES    1
#define VBE_DISPI_INDEX_YRES    2
#define VBE_DISPI_INDEX_BPP     3
#define VBE_DISPI_INDEX_ENABLE  4
#define VBE_DISPI_INDEX_BANK    5
#define VBE_DISPI_INDEX_VWIDTH  6
#define VBE_DISPI_INDEX_VHEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET 8
#define VBE_DISPI_INDEX_Y_OFFSET 9

#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40   /* linear framebuffer */

#define VBE_DISPI_ID_MIN        0xB0C0
#define VBE_DISPI_ID_MAX        0xB0C5

static int bga_found = 0;
static uint64_t last_fb_phys = 0;

static void bga_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA,  value);
}

static uint16_t bga_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

int bga_available(void) { return bga_found; }

int bga_init(uint16_t width, uint16_t height, uint16_t bpp)
{
    /* check BGA is present */
    uint16_t id = bga_read(VBE_DISPI_INDEX_ID);
    if (id < VBE_DISPI_ID_MIN || id > VBE_DISPI_ID_MAX)
        return -1;

    /* disable display during mode set */
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

    bga_write(VBE_DISPI_INDEX_XRES,   width);
    bga_write(VBE_DISPI_INDEX_YRES,   height);
    bga_write(VBE_DISPI_INDEX_BPP,    bpp);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);

    /* enable with linear framebuffer */
    bga_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    /*
     * آدرس ثابت هاردکد قابل اعتماد نیست — بسته به machine type و
     * نسخهٔ QEMU عوض میشه. واقعی‌ترین راه خوندن BAR0 از PCI config
     * space برای دستگاه VGA/display هست.
     */
    uint64_t fb_phys = pci_find_vga_bar0();
    if (!fb_phys)
        fb_phys = BGA_ADDR;   /* fallback: آدرس متداول -vga std در QEMU pc */

    last_fb_phys = fb_phys;

    /* tell fb driver where the buffer is */
    uint32_t pitch = width * (bpp / 8);
    fb_set_info(fb_phys, pitch, width, height, (uint8_t)bpp);

    bga_found = 1;
    return 0;
}

uint64_t bga_fb_phys_addr(void) { return last_fb_phys; }
