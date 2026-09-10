CC     := gcc
CFLAGS := -m64 -ffreestanding -nostdlib -nostdinc \
          -O2 -Wall -I../libc/include \
          -mno-red-zone -mno-sse -mno-sse2 \
          -fno-stack-protector
LDFLAGS := -m elf_x86_64 -nostdlib -T linker.ld
LIBC    := ../libc/libc.a

PROGS := ls.elf cat.elf echo.elf

all: $(PROGS)

ls.elf: ls.o $(LIBC)
	ld $(LDFLAGS) -o $@ $^

cat.elf: cat.o $(LIBC)
	ld $(LDFLAGS) -o $@ $^

echo.elf: echo.o $(LIBC)
	ld $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(PROGS)
