#!/bin/bash
# Populate the Sinux ext2 disk image with the distro userland.
#
# Uses debugfs (e2fsprogs, already a CI dependency) — no root and no
# loop mounts needed. Safe to re-run: directories are created if
# missing, files are overwritten with the freshly built ELFs.
set -u

DISK="sinux.img"

# The in-kernel ext2 reader only follows the 12 direct blocks
# (4 KiB each with our mkfs options): refuse oversized binaries
# instead of installing a truncated, unbootable ELF.
MAX_BYTES=49152

[ -f "$DISK" ] || { echo "[FAIL] $DISK not found (mkdisk.sh first)"; exit 1; }

fail=0
install_one() {  # $1 = local ELF, $2 = path inside the image
    local src="$1" dst="$2"
    [ -f "$src" ] || {
        echo "[FAIL] missing build artifact: $src (run 'make userspace' first)"
        fail=1; return
    }
    local size
    size=$(stat -c%s "$src")
    if [ "$size" -gt "$MAX_BYTES" ]; then
        echo "[FAIL] $src is $size bytes (> $MAX_BYTES: beyond the 12-direct-block reader)"
        fail=1; return
    fi
    debugfs -w -R "write $src $dst" "$DISK" >/dev/null 2>&1 \
        || { echo "[FAIL] debugfs write $dst"; fail=1; return; }
    echo "[OK] $dst ($size bytes)"
}

debugfs -w -R "mkdir sbin" "$DISK" >/dev/null 2>&1 || true
debugfs -w -R "mkdir bin"  "$DISK" >/dev/null 2>&1 || true

install_one userspace/init/init.elf      sbin/init
install_one userspace/sinush/sinush.elf  bin/sinush
install_one userspace/coreutils/ls.elf   bin/ls
install_one userspace/coreutils/cat.elf  bin/cat
install_one userspace/coreutils/echo.elf bin/echo
install_one userspace/hello/hello.elf    bin/hello

if [ "$fail" -ne 0 ]; then
    echo "[FAIL] disk populate failed"
    exit 1
fi

echo "--- image sbin ---"
debugfs -R "ls -l sbin" "$DISK" 2>/dev/null
echo "--- image bin ---"
debugfs -R "ls -l bin" "$DISK" 2>/dev/null
echo "[OK] disk populated"
