// kernel.c: Sample "kernel" file for testing
#include <stdint.h>
#include "efi.h"
#include "efi_lib.h"

// Stub function for arch_map_page (not used by this kernel, but required by efi_lib.h)
__attribute__((weak)) void arch_map_page(uint64_t physical_address, uint64_t virtual_address, Memory_Map_Info *mmap) {
    // This function is not used by the kernel, but is required by efi_lib.h
    (void)physical_address;
    (void)virtual_address;
    (void)mmap;
}

typedef struct {
    Memory_Map_Info mmap;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE gop_mode;
} Kernel_Params;

void EFIAPI kmain(Kernel_Params kargs) {
    // Grab Framebuffer/GOP info
    UINT32 *fb = (UINT32 *)kargs.gop_mode.FrameBufferBase; // BGRA8888
    UINT32 xres = kargs.gop_mode.Info->PixelsPerScanLine;
    UINT32 yres = kargs.gop_mode.Info->VerticalResolution;
    
    // Clear screen to solid color
    for (UINT32 y = 0; y < yres; y++) {
        for (UINT32 x = 0; x < xres; x++) {
            fb[y*xres + x] = 0xFFDDDDDD; // Light Gray AARRGGBB 8888
        }
    }
    
    // Draw a smaller rectangle in top-left corner
    for (UINT32 y = 0; y < yres / 5; y++) {
        for (UINT32 x = 0; x < xres / 5; x++) {
            fb[y*xres + x] = 0xFFCC2222; // AARRGGBB 8888
        }
    }
}
