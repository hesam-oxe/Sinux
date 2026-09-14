#!/bin/bash
# Sinux headless QEMU smoke test.
#
# Boots the ISO, captures serial output, and asserts the boot markers.
# Exit 0 = kernel booted cleanly. Anything else = failure.
#
# NOTE: QEMU is invoked directly (same flags as `run-serial` in
# scripts/qemu.mk) instead of via `make run-serial`, so that `timeout`
# signals QEMU itself. Through make, QEMU would be orphaned and the
# timeout would never stop it.
set -u

ISO="build/iso/sinux.iso"
DISK="sinux.img"
LOG="serial.log"
TIMEOUT_S="${SMOKE_TIMEOUT:-120}"

[ -f "$ISO" ]  || { echo "[FAIL] $ISO not found (run 'make iso' first)"; exit 1; }
[ -f "$DISK" ] || { echo "[FAIL] $DISK not found (run 'make iso' first)"; exit 1; }

echo "[..] booting $ISO in QEMU (headless, SMP-2, ${TIMEOUT_S}s timeout)..."
timeout --signal=TERM --kill-after=10s "$TIMEOUT_S" \
  qemu-system-x86_64 \
    -cdrom "$ISO" -m 256M -device bochs-display \
    -smp 2 \
    -drive "file=${DISK},format=raw,if=ide,index=0,media=disk,file.locking=off" \
    -drive "file=${DISK},format=raw,if=none,id=vdrv,readonly=on,file.locking=off" \
    -device virtio-blk-pci,drive=vdrv,disable-legacy=off,disable-modern=on,queue-size=128 \
    -nographic -serial mon:stdio -no-reboot \
    > "$LOG" 2>&1 || code=$?
code=${code:-0}
echo "[..] qemu exit code: $code (124/137 = expected timeout; kernel runs forever)"

fail=0
check_present() {
  if grep -a -q "$1" "$LOG"; then echo "[OK] marker found: $1";
  else echo "[FAIL] marker missing: $1"; fail=1; fi
}
check_absent() {
  if grep -a -q "$1" "$LOG"; then echo "[FAIL] forbidden marker present: $1"; fail=1;
  else echo "[OK] absent as expected: $1"; fi
}

# Boot banner (tty_puts -> serial), memory line, kernel self-tests.
check_present "Made By SUN"
check_present "RAM:"
check_present "ktest: ALL TESTS PASSED"
# SMP: booted with -smp 2, both CPUs must come online.
check_present "smp: Total CPUs online: 2"
# Distro disk: ext2 mount, rootfs seeding, init spawn.
check_present "ext2: mounted /mnt/disk"
check_present "rootfs: /sbin/init ready"
check_present "queued from /sbin/init"
# virtio-blk data path self-test (read-only attached drive).
check_present "virtio-blk: self-test read OK"
# Interactive shell: prompt plus self-test transcript responses.
check_present "sinux>"
check_present "Sinux speaks!"
check_present "sinush"
# The kernel fallback shell must NOT appear: userland owns the console.
check_absent  "falling back to kernel shell"
check_absent  "panic"

if [ "$fail" -ne 0 ]; then
  echo "=== $LOG (tail) ==="
  tail -40 "$LOG"
  exit 1
fi
echo "[OK] smoke test passed"
