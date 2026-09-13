#include "virtio_blk.h"
#include "pci.h"
#include "../lib/io.h"
#include "../lib/string.h"
#include "../lib/printk.h"
#include <stdint.h>
#include <stdbool.h>

/* ── legacy virtio PCI register offsets (I/O BAR) ───────────── */
#define VIRTIO_REG_FEATURES   0x00  /* R: device features (32)  */
#define VIRTIO_REG_GUEST_FEAT 0x04  /* W: guest features (32)   */
#define VIRTIO_REG_QADDR      0x08  /* W: queue PFN (32)        */
#define VIRTIO_REG_QMAX       0x0C  /* R: queue size max (16)   */
#define VIRTIO_REG_QSEL       0x0E  /* W: queue select (16)     */
#define VIRTIO_REG_QNOTIFY    0x10  /* W: queue notify (16)     */
#define VIRTIO_REG_STATUS     0x12  /* RW: device status (8)    */
#define VIRTIO_REG_ISR        0x13  /* R: ISR status (8)        */
#define VIRTIO_REG_CONFIG     0x14  /* device config space      */

/* status bits */
#define VIRTIO_S_ACK        0x01
#define VIRTIO_S_DRIVER     0x02
#define VIRTIO_S_DRIVER_OK  0x04
#define VIRTIO_S_FEAT_OK    0x08
#define VIRTIO_S_FAILED     0x80

/* block request types / status */
#define VIRTIO_BLK_T_IN   0
#define VIRTIO_BLK_T_OUT  1
#define VIRTIO_BLK_S_OK   0

/* descriptor flags */
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VBLK_QSIZE   128
#define VBLK_MAXSEC  8          /* 8 * 512 = one page per transfer */

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vring_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VBLK_QSIZE];
} vring_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} vring_used_elem_t;

typedef struct {
    uint16_t        flags;
    uint16_t        idx;
    vring_used_elem_t ring[VBLK_QSIZE];
} vring_used_t;

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} vblk_req_t;

/*
 * Static ring + buffers.  The kernel image (and these .bss arrays)
 * are identity-mapped, so guest-physical == virtual address — the
 * same assumption the rest of the kernel relies on.
 */
static uint8_t      vblk_ring[8192] __attribute__((aligned(4096)));
static uint8_t      vblk_buf[4096]  __attribute__((aligned(4096)));
static vblk_req_t   vblk_req;
static uint8_t      vblk_status;

static uint16_t     vblk_iobase = 0;
static bool         vblk_ok     = false;

#define VBLK_DESC  ((vring_desc_t *)(void *)vblk_ring)
#define VBLK_AVAIL ((vring_avail_t *)(void *)(vblk_ring + VBLK_QSIZE * 16))
#define VBLK_USED  ((vring_used_t *)(void *)(vblk_ring + 4096))

static bool
vblk_transfer(bool write, uint64_t sector, const void *buf, uint32_t nsec)
{
    if (!vblk_ok) return false;
    if (nsec == 0 || nsec > VBLK_MAXSEC || !buf) return false;

    vblk_req.type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    vblk_req.reserved = 0;
    vblk_req.sector   = sector;
    vblk_status       = 0xFF;

    vring_desc_t *d = VBLK_DESC;
    d[0].addr  = (uint64_t)(uintptr_t)&vblk_req;
    d[0].len   = sizeof(vblk_req);
    d[0].flags = VRING_DESC_F_NEXT;
    d[0].next  = 1;

    d[1].addr  = (uint64_t)(uintptr_t)buf;
    d[1].len   = nsec * 512;
    d[1].flags = VRING_DESC_F_NEXT | (write ? 0 : VRING_DESC_F_WRITE);
    d[1].next  = 2;

    d[2].addr  = (uint64_t)(uintptr_t)&vblk_status;
    d[2].len   = 1;
    d[2].flags = VRING_DESC_F_WRITE;
    d[2].next  = 0;

    vring_avail_t *av = VBLK_AVAIL;
    vring_used_t  *us = VBLK_USED;

    av->ring[av->idx % VBLK_QSIZE] = 0;
    __asm__ volatile("" ::: "memory");
    av->idx++;

    outw(vblk_iobase + VIRTIO_REG_QNOTIFY, 0);

    uint16_t want = av->idx;
    volatile uint32_t spin = 0;
    while (us->idx != want) {
        __asm__ volatile("pause");
        if (++spin > 20000000u) {
            printk(KERN_ERR "virtio-blk: transfer timeout\n");
            return false;
        }
    }
    __asm__ volatile("" ::: "memory");

    return vblk_status == VIRTIO_BLK_S_OK;
}

bool
virtio_blk_read(uint64_t lba, uint8_t count, void *buf)
{
    return vblk_transfer(false, lba, buf, count);
}

bool
virtio_blk_write(uint64_t lba, uint8_t count, const void *buf)
{
    return vblk_transfer(true, lba, buf, count);
}

bool
virtio_blk_present(void)
{
    return vblk_ok;
}

void
virtio_blk_init(void)
{
    uint8_t bus, dev, func;

    if (pci_find_device(0x1AF4, 0x1001, &bus, &dev, &func) != 0) {
        printk(KERN_INFO "virtio-blk: no device present\n");
        return;
    }

    uint64_t bar = pci_read_bar(bus, dev, func, 0);
    /* pci_read_bar() normalizes an I/O BAR to its base address with the
     * low flag bits already stripped, so a non-zero value IS the I/O
     * base — testing the I/O-space bit here would always fail. */
    if (!bar) {
        printk(KERN_WARNING "virtio-blk: device has no usable BAR\n");
        return;
    }
    vblk_iobase = (uint16_t)bar;

    /* enable I/O space + bus mastering */
    pci_write_cmd(bus, dev, func, 0x0005);

    outb(vblk_iobase + VIRTIO_REG_STATUS, 0);
    outb(vblk_iobase + VIRTIO_REG_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER);

    uint32_t feats = inl(vblk_iobase + VIRTIO_REG_FEATURES);
    (void)feats;
    outl(vblk_iobase + VIRTIO_REG_GUEST_FEAT, 0);

    outw(vblk_iobase + VIRTIO_REG_QSEL, 0);
    uint16_t qmax = inw(vblk_iobase + VIRTIO_REG_QMAX);
    if (qmax == 0 || qmax > VBLK_QSIZE) {
        printk(KERN_ERR "virtio-blk: bad queue size %u\n",
               (uint64_t)qmax);
        outb(vblk_iobase + VIRTIO_REG_STATUS, VIRTIO_S_FAILED);
        return;
    }

    kmemset(vblk_ring, 0, sizeof(vblk_ring));
    outl(vblk_iobase + VIRTIO_REG_QADDR,
         (uint32_t)(((uint64_t)(uintptr_t)vblk_ring) >> 12));

    outb(vblk_iobase + VIRTIO_REG_STATUS,
         VIRTIO_S_ACK | VIRTIO_S_DRIVER | VIRTIO_S_FEAT_OK);
    if (!(inb(vblk_iobase + VIRTIO_REG_STATUS) & VIRTIO_S_FEAT_OK)) {
        printk(KERN_ERR "virtio-blk: features rejected\n");
        outb(vblk_iobase + VIRTIO_REG_STATUS, VIRTIO_S_FAILED);
        return;
    }
    outb(vblk_iobase + VIRTIO_REG_STATUS,
         VIRTIO_S_ACK | VIRTIO_S_DRIVER | VIRTIO_S_FEAT_OK |
         VIRTIO_S_DRIVER_OK);

    uint32_t cap_lo = inl(vblk_iobase + VIRTIO_REG_CONFIG + 0);
    uint32_t cap_hi = inl(vblk_iobase + VIRTIO_REG_CONFIG + 4);
    uint64_t cap = ((uint64_t)cap_hi << 32) | cap_lo;

    printk(KERN_INFO "virtio-blk: ready at io=0x%x qsize=%u sectors=%u\n",
           (uint64_t)vblk_iobase, (uint64_t)qmax,
           cap > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : cap);

    vblk_ok = true;

    /* Self-test: the distro disk's ext2 superblock lives at byte
     * 1024 (sectors 2-3); magic 0xEF53 sits at offset 56. */
    if (virtio_blk_read(2, 2, vblk_buf)) {
        uint16_t magic = (uint16_t)vblk_buf[56] |
                         ((uint16_t)vblk_buf[57] << 8);
        if (magic == 0xEF53)
            printk(KERN_INFO "virtio-blk: self-test read OK "
                             "(ext2 superblock magic present)\n");
        else
            printk(KERN_WARNING "virtio-blk: self-test read got "
                                "magic 0x%x, want 0xef53\n",
                   (uint64_t)magic);
    } else {
        printk(KERN_ERR "virtio-blk: self-test read FAILED\n");
    }
}
