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

UINTN get_color(UINTN choice){
    switch (choice) {
        case 1:
            return 0xFFDDDDDD;
        case 2:
            return 0xFFCC2222;
        default:
            break;
    }
    return choice;
}

typedef struct {
    Memory_Map_Info mmap;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE gop_mode;
    EFI_RUNTIME_SERVICES              *RuntimeServices;
    UINTN                             NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE           *ConfigurationTable;
} Kernel_Params;


__attribute__((section(".kernel"), aligned(0x1000))) void EFIAPI kmain(Kernel_Params* kargs) {
    // Grab Framebuffer/GOP info
    UINT32 *fb = (UINT32 *)kargs->gop_mode.FrameBufferBase; // BGRA8888
    UINT32 xres = kargs->gop_mode.Info->PixelsPerScanLine;
    UINT32 yres = kargs->gop_mode.Info->VerticalResolution;
    
    // Clear screen to solid color
    UINTN color = get_color(1);
    for (UINT32 y = 0; y < yres; y++) {
        for (UINT32 x = 0; x < xres; x++) {
            fb[y*xres + x] = color; // Light Gray AARRGGBB 8888
        }
    }
    
    // Draw a smaller rectangle in top-left corner
    color = get_color(2);
    for (UINT32 y = 0; y < yres / 5; y++) {
        for (UINT32 x = 0; x < xres / 5; x++) {
            fb[y*xres + x] = color; // AARRGGBB 8888
        }
    }
    
    
    EFI_TIME old_time = {0}, new_time = {0};
    EFI_TIME_CAPABILITIES time_cap = {0};
    UINTN i = 0;
    while (i < 3){
        kargs->RuntimeServices->GetTime(&new_time, &time_cap);
        if (old_time.Second != new_time.Second){
            i++;
            old_time.Second = new_time.Second;
        }
    }
    
    kargs->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    
    __builtin_unreachable();
    
}
