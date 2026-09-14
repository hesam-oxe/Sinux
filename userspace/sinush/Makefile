CC     := gcc
CFLAGS := -m64 -ffreestanding -nostdlib -nostdinc -isystem $(shell $(CC) -print-file-name=include) \
          -O2 -Wall -I../libc/include \
          -mno-red-zone -mno-sse -mno-sse2 \
          -fno-stack-protector
LDFLAGS := -m elf_x86_64 -nostdlib -T linker.ld
LIBC    := ../libc/libc.a

all: sinush.elf

sinush.elf: sinush.o $(LIBC)
	ld $(LDFLAGS) -o $@ $^

sinush.o: sinush.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f sinush.o sinush.elf
