#!/bin/bash
DISK="sinux.img"
SIZE_MB=128

if [ -f "$DISK" ]; then
    echo "[..] $DISK exists, reusing (delete it to re-create)"
else
    echo "[..] Creating $DISK ($SIZE_MB MiB)..."
    dd if=/dev/zero of=$DISK bs=1M count=$SIZE_MB status=none
    # -b 4096: matches the 4 KiB block_buf in kernel/fs/ext2.c
    # -I 128:  the driver assumes 128-byte inodes
    # -F:      non-interactive (target is a regular file, not a device)
    mkfs.ext2 -F -L "sinux" -b 4096 -I 128 $DISK
    echo "[OK] $DISK created"
fi

# (Re)install the distro userland on every build so the image never
# goes stale relative to the freshly built ELFs.
./scripts/populate-disk.sh
