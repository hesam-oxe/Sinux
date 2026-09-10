#pragma once
#include <stdbool.h>
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
char serial_getc(void);

/* Non-blocking receive check: true when a byte waits in COM1. */
bool serial_has_data(void);
