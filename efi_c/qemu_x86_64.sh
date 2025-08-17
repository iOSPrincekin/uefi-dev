#!/bin/sh

# Sendin' Out a TEST O S
qemu-system-x86_64 \
-drive format=raw,file=../UEFI-GPT-image-creator/test.hdd \
-bios ../UEFI-GPT-image-creator/bios64.bin \
-m 256M \
-vga std \
-display gtk,gl=on,zoom-to-fit=off,window-close=on \
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
