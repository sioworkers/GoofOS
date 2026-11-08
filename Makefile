BUILD := build
CC := gcc
LD := ld
OBJCOPY := objcopy
MAX_KERNEL_SECTORS := 127
IMAGE_SIZE := 1474560

CXXFLAGS := -m32 -march=i386 -mno-sse -mno-mmx -msoft-float \
	-std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -Wall -Wextra -O2

.PHONY: all run clean

all: $(BUILD)/goofos.img

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.o: src/boot/boot.s | $(BUILD)
	$(CC) -m32 -c $< -o $@

$(BUILD)/boot.elf: $(BUILD)/boot.o boot.ld
	$(LD) -m elf_i386 -T boot.ld -o $@ $(BUILD)/boot.o

$(BUILD)/boot.bin: $(BUILD)/boot.elf
	$(OBJCOPY) -O binary $< $@
	test "$$(wc -c < $@)" -eq 512

$(BUILD)/kernel.o: src/kernel/kernel.cpp | $(BUILD)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/kernel.o kernel.ld
	$(LD) -m elf_i386 -T kernel.ld -nostdlib -o $@ $(BUILD)/kernel.o

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/kernel_sectors.inc: $(BUILD)/kernel.bin
	@sectors=$$(( ($$(wc -c < $<) + 511) / 512 )); \
	if [ "$$sectors" -gt $(MAX_KERNEL_SECTORS) ]; then \
		echo "Kernel exceeds the $(MAX_KERNEL_SECTORS)-sector BIOS loader limit" >&2; exit 1; \
	fi; \
	printf '.word %s\n' "$$sectors" > $@

$(BUILD)/header.o: src/boot/header.s $(BUILD)/kernel_sectors.inc
	$(CC) -m32 -c $< -o $@

$(BUILD)/header.bin: $(BUILD)/header.o
	$(OBJCOPY) -O binary $< $@
	truncate -s 512 $@

$(BUILD)/goofos.img: $(BUILD)/boot.bin $(BUILD)/header.bin $(BUILD)/kernel.bin
	cp $(BUILD)/boot.bin $@
	cat $(BUILD)/header.bin >> $@
	cat $(BUILD)/kernel.bin >> $@
	truncate -s $(IMAGE_SIZE) $@

run: $(BUILD)/goofos.img
	qemu-system-i386 -drive format=raw,file=$<

clean:
	rm -rf $(BUILD)
