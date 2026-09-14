CC     := gcc
CFLAGS := -m64 -ffreestanding -nostdlib -nostdinc -isystem $(shell $(CC) -print-file-name=include) \
          -O2 -Wall -I../libc/include \
          -mno-red-zone -mno-sse -mno-sse2 \
          -fno-stack-protector
LDFLAGS := -m elf_x86_64 -nostdlib -T linker.ld
LIBC    := ../libc/libc.a

all: init.elf

init.elf: init.o $(LIBC)
	ld $(LDFLAGS) -o $@ $^

init.o: init.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f init.o init.elf
