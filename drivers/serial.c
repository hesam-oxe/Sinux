#include "serial.h"
#include "../lib/io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1+1,0x00); outb(COM1+3,0x80);
    outb(COM1+0,0x03); outb(COM1+1,0x00);
    outb(COM1+3,0x03); outb(COM1+2,0xC7); outb(COM1+4,0x0B);
}

void serial_putc(char c) {
    /* Bounded wait for THR-empty: a real 16550 (or an emulator backend
     * under backpressure) can hold the flag clear for a long time, and an
     * unbounded spin here freezes the whole kernel on console output. */
    for (int i = 0; i < 100000; i++) {
        if (inb(COM1 + 5) & 0x20) {
            break;
        }
    }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) { while (*s) serial_putc(*s++); }

char serial_getc(void) {
    while (!(inb(COM1+5) & 0x01)) {}
    return (char)inb(COM1);
}

bool serial_has_data(void) {
    return (inb(COM1+5) & 0x01) != 0;
}
