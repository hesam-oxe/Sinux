#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
 * Legacy virtio-blk block driver (transitional PCI device
 * 0x1AF4:0x1001, I/O-port transport).
 *
 * QEMU provides one with e.g.:
 *   -drive file=disk.img,format=raw,if=none,id=vdrv,readonly=on
 *   -device virtio-blk-pci,drive=vdrv
 *
 * Probing is safe: without the device, init just reports absence.
 * With it, init negotiates queue 0, reads the capacity from config
 * space and performs a read self-test.  All transfers are polled
 * (no interrupts) and use static identity-mapped buffers, matching
 * the kernel's identity-map design (see proc_spawn_init notes).
 */
void virtio_blk_init(void);
bool virtio_blk_present(void);

/* Polled sector I/O.  count*512 must fit in 4 KiB (count <= 8).
 * Returns true on VIRTIO_BLK_S_OK. */
bool virtio_blk_read(uint64_t lba, uint8_t count, void *buf);
bool virtio_blk_write(uint64_t lba, uint8_t count, const void *buf);
