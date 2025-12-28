#!/bin/sh

# Sendin' Out a TEST O S
# Detect display backend based on OS
if [ "$(uname)" = "Darwin" ]; then
    DISPLAY_BACKEND="cocoa"
else
    DISPLAY_BACKEND="gtk,gl=on,zoom-to-fit=off,window-close=on"
fi

# Build QEMU command
QEMU_CMD="qemu-system-x86_64 \\
-drive format=raw,file=../UEFI-GPT-image-creator/test.hdd \\
-bios ../UEFI-GPT-image-creator/bios64.bin \\
-m 256M \\
-vga std \\
-display $DISPLAY_BACKEND \\
-name TESTOS \\
-machine q35 \\
-usb \\
-device usb-mouse \\
-rtc base=localtime \\
-net none"

# Print detailed command
echo "=========================================="
echo "Executing QEMU with the following command:"
echo "=========================================="
echo "$QEMU_CMD"
echo "=========================================="
echo ""

# Execute QEMU
qemu-system-x86_64 \
-drive format=raw,file=../UEFI-GPT-image-creator/test.hdd \
-bios ../UEFI-GPT-image-creator/bios64.bin \
-m 256M \
-vga std \
-display $DISPLAY_BACKEND \
-name TESTOS \
-machine q35 \
-usb \
-device usb-mouse \
-rtc base=localtime \
-net none


# For pc speaker audio, add these lines depending on your sound backend
#   for "-audiodev <backend>":
# pa       = pulseaudio
# alsa     = alsa
# pipewire = pipewire

#-audiodev pa,id=speaker \
#-machine pcspk-audiodev=speaker \
