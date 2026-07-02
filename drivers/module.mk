OBJS += \
    $(BUILD)/drivers/serial.o   \
    $(BUILD)/drivers/tty.o      \
    $(BUILD)/drivers/keyboard.o \
    $(BUILD)/drivers/ata.o      \
    $(BUILD)/drivers/fb.o       \
    $(BUILD)/drivers/pci.o      \
    $(BUILD)/drivers/bga.o

$(BUILD)/drivers/%.o: drivers/%.c | $(BUILD)/drivers
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/drivers:
	mkdir -p $@
