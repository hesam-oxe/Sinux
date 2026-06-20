#include "tty.h"
#include "vga.h"
#include "serial.h"

void tty_init(void) {
    serial_init();
    vga_init();
}

void tty_putc(char c) { vga_putc(c); serial_putc(c); }
void tty_puts(const char *s) { while(*s) tty_putc(*s++); }

void tty_setcolor_info(void)  { vga_setcolor(LCYAN,  BLACK); }
void tty_setcolor_err(void)   { vga_setcolor(LRED,   BLACK); }
void tty_setcolor_ok(void)    { vga_setcolor(LGREEN, BLACK); }
void tty_setcolor_reset(void) { vga_setcolor(WHITE,  BLACK); }

/* ───────────────────────────────────────────────
 * TTY character device  (/dev/tty0)
 * ─────────────────────────────────────────────── */
#include "../kernel/fs/vfs.h"
#include "../lib/printk.h"
#include "keyboard.h"

/*
 * Canonical (cooked) read:
 *   – blocks until newline
 *   – echoes each character back to screen
 *   – handles backspace visually
 */
static int64_t
tty_dev_read(file_t *f, void *buf, size_t n)
{
    (void)f;
    if (n == 0) return 0;

    char  *b = (char *)buf;
    size_t i = 0;

    while (i < n) {
        char c = kbd_getc();

        if (c == '\b' || c == 127) {       /* backspace */
            if (i > 0) {
                i--;
                tty_putc('\b');            /* VGA: col--, write space */
            }
            continue;
        }

        tty_putc(c);                       /* echo */
        b[i++] = c;

        if (c == '\n') break;              /* line done */
    }
    return (int64_t)i;
}

static int64_t
tty_dev_write(file_t *f, const void *buf, size_t n)
{
    (void)f;
    const char *s = (const char *)buf;
    for (size_t i = 0; i < n; i++) tty_putc(s[i]);
    return (int64_t)n;
}

static fs_ops_t tty_dev_ops = {
    .read  = tty_dev_read,
    .write = tty_dev_write,
};

/*
 * Call after vfs_init() + ramfs_mount("/").
 * Creates /dev/tty0 in ramfs then replaces its ops
 * with our character-device ops — no new inode type needed.
 */
void
tty_dev_init(void)
{
    vfs_create("/dev/tty0", FT_CHR);

    inode_t *ino = vfs_lookup("/dev/tty0");
    if (!ino) {
        printk(KERN_ERR "tty: failed to register /dev/tty0\n");
        return;
    }

    ino->ops = &tty_dev_ops;
    printk(KERN_INFO "tty: /dev/tty0 registered\n");
}
