BUILD := build
CC := gcc
HOSTCXX := g++
LD := ld
OBJCOPY := objcopy
MAX_KERNEL_SECTORS := 127
IMAGE_SIZE := 1474560

CXXFLAGS := -m32 -march=i386 -mno-sse -mno-mmx -msoft-float \
	-std=c++17 -ffreestanding -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -Wall -Wextra -O2

.PHONY: all run test clean

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

$(BUILD)/kernel.o: src/kernel/kernel.cpp src/kernel/console.hpp src/kernel/gex.hpp | $(BUILD)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(BUILD)/console.o: src/kernel/console.cpp src/kernel/console.hpp | $(BUILD)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: src/kernel/keyboard.cpp src/kernel/keyboard.hpp | $(BUILD)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(BUILD)/gex.o: src/kernel/gex.cpp src/kernel/gex.hpp src/kernel/console.hpp src/kernel/keyboard.hpp | $(BUILD)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel_entry.o: src/kernel/entry.s | $(BUILD)
	$(CC) -m32 -c $< -o $@

$(BUILD)/goofc: tools/goofc.cpp | $(BUILD)
	$(HOSTCXX) -std=c++17 -O2 -Wall -Wextra -o $@ $<

$(BUILD)/main.gex: programs/main.gof $(BUILD)/goofc
	$(BUILD)/goofc $< -o $@

$(BUILD)/program.o: src/kernel/program.s $(BUILD)/main.gex
	$(CC) -m32 -c $< -o $@

KERNEL_OBJS := $(BUILD)/kernel_entry.o $(BUILD)/kernel.o $(BUILD)/console.o \
	$(BUILD)/keyboard.o $(BUILD)/gex.o $(BUILD)/program.o

$(BUILD)/kernel.elf: $(KERNEL_OBJS) kernel.ld
	$(LD) -m elf_i386 -T kernel.ld -nostdlib -o $@ $(KERNEL_OBJS)

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
	$(OBJCOPY) -O binary -j .header $< $@
	truncate -s 512 $@

$(BUILD)/goofos.img: $(BUILD)/boot.bin $(BUILD)/header.bin $(BUILD)/kernel.bin
	cp $(BUILD)/boot.bin $@
	cat $(BUILD)/header.bin >> $@
	cat $(BUILD)/kernel.bin >> $@
	truncate -s $(IMAGE_SIZE) $@

run: $(BUILD)/goofos.img
	qemu-system-i386 -drive format=raw,file=$<

test: $(BUILD)/goofos.img
	python3 tools/boottest.py $<

clean:
	rm -rf $(BUILD)
