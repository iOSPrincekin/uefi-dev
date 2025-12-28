.POSIX:
.PHONY: all clean

SOURCE = efi.c
TARGET = BOOTX64.EFI

ifeq ($(OS), Windows_NT)
QEMU = ./qemu.bat
DISK_FLAGS = --vhd
else
QEMU = ./qemu.sh
DISK_FLAGS =
endif

# Use clang by default (works on macOS and Linux)
# For gcc/mingw-w64, uncomment the gcc section below and comment out clang
CC = clang \
	-target x86_64-unknown-windows \
	-fuse-ld=lld-link \
	-Wl,-subsystem:efi_application \
	-Wl,-entry:efi_main

# Uncomment for gcc/mingw-w64 (requires x86_64-w64-mingw32-gcc to be installed)
#CC = x86_64-w64-mingw32-gcc \
#	-Wl,--subsystem,10 \
#	-e efi_main

CFLAGS = \
	-std=c17 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-mno-red-zone \
	-ffreestanding \
	-nostdlib 

DISK_IMG_FOLDER = ../UEFI-GPT-image-creator
DISK_IMG_PGM    = write_gpt

all: $(DISK_IMG_FOLDER)/$(DISK_IMG_PGM) $(TARGET)
	cd $(DISK_IMG_FOLDER) && $(QEMU)

$(DISK_IMG_FOLDER)/$(DISK_IMG_PGM):
	cd $(DISK_IMG_FOLDER) && $(MAKE)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $@ $<
	cp $(TARGET) $(DISK_IMG_FOLDER); \
	cd $(DISK_IMG_FOLDER) && ./$(DISK_IMG_PGM) $(DISK_FLAGS)

clean:
	rm -rf $(TARGET)

