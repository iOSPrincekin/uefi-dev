#include <stdarg.h>

#include "efi.h"
#include "efi_lib.h"

#define arch_header <arch/ARCH/ARCH.h>
#include arch_header


#ifdef __clang__
int _fltused = 0;   // If using floating point code & lld-link, need to define this
#endif

#define ARRAY_SIZE(x) (sizeof (x) / sizeof (x)[0])

#define SCANCODE_UP_ARROW   0x1
#define SCANCODE_DOWN_ARROW 0x2
#define SCANCODE_ESC        0x17


#define DEFAULT_FG_COLOR        EFI_YELLOW
#define DEFAULT_BG_COLOR        EFI_BLUE

#define HIGHLIGHT_FG_COLOR      EFI_BLUE
#define HIGHLIGHT_BG_COLOR      EFI_CYAN

// EFI_GRAPHICS_OUTPUT_BLT_PIXEL values, BGRr8888
#define px_LGRAY {0xEE,0xEE,0xEE,0x00}
#define px_BLACK {0x00,0x00,0x00,0x00}
#define px_BLUE  {0x98,0x00,0x00,0x00}  // EFI_BLUE

// Kernel start address in higher memory (64-bit) - last 2 GiBs of virtual memory
#define KERNEL_START_ADDRESS 0xFFFFFFFF80000000


EFI_BOOT_SERVICES                         *bs;
EFI_RUNTIME_SERVICES                      *rs;
EFI_SYSTEM_TABLE                          *st;

EFI_EVENT timer_event;  // Global timer event

Page_Table *pml4;

void init_global_varibles(EFI_HANDLE handle, EFI_SYSTEM_TABLE *systable){
    cout = systable->ConOut;
    cin = systable->ConIn;
    st = systable;
    bs = systable->BootServices;
    rs = systable->RuntimeServices;
    image = handle;
}

// ====================
// Set Text Mode
// ====================
EFI_STATUS set_text_mode(void) {
    // Store found Text mode info
    typedef struct {
        INTN  mode;
        UINTN cols;
        UINTN rows;
    } Text_Mode_Info;
    
    Text_Mode_Info text_modes[20];
    
    UINTN mode_index = 0;   // Current mode within entire menu of text mode choices
    
    // Overall screen loop
    while (true) {
        cout->ClearScreen(cout);
        
        // Get current text mode info
        UINTN max_cols = 0, max_rows = 0;
        cout->QueryMode(cout, cout->Mode->Mode, &max_cols, &max_rows);
        
        printf_c16(u"Text mode information:\r\n"
                   u"Max Mode: %d\r\n"
                   u"Current Mode: %d\r\n"
                   u"Attribute: %x\r\n"
                   u"CursorColumn: %d\r\n"
                   u"CursorRow: %d\r\n"
                   u"CursorVisible: %d\r\n"
                   u"Columns: %d\r\n"
                   u"Rows: %d\r\n\r\n",
                   cout->Mode->MaxMode,
                   cout->Mode->Mode,
                   cout->Mode->Attribute,
                   cout->Mode->CursorColumn,
                   cout->Mode->CursorRow,
                   cout->Mode->CursorVisible,
                   max_cols,
                   max_rows);
        
        printf_c16(u"Available text modes:\r\n");
        
        UINTN menu_top = cout->Mode->CursorRow;
        
        // Print keybinds at bottom of screen
        cout->SetCursorPosition(cout, 0, max_rows-3);
        printf_c16(u"Up/Down Arrow = Move Cursor\r\n"
                   u"Enter = Select\r\n"
                   u"Escape = Go Back");
        
        UINTN menu_bottom = max_rows-5;    // Stop above keybind text (0-based offset)
        
        // Get all valid text modes' info
        // NOTE: Max valid GOP mode is ModeMax-1 per UEFI spec
        UINT32 max = cout->Mode->MaxMode;
        if (max-1 < menu_bottom - menu_top) menu_bottom = menu_top + max-1;
        
        UINT32 num_modes = 0;
        for (UINT32 i = 0; i < ARRAY_SIZE(text_modes) && i < max; i++) {
            // If mode is bad or rows/cols are invalid, go on
            if (cout->QueryMode(cout, i, &text_modes[num_modes].cols, &text_modes[num_modes].rows) != EFI_SUCCESS ||
                ((text_modes[num_modes].cols < 10 || text_modes[num_modes].cols > 999) ||
                 (text_modes[num_modes].rows < 10 || text_modes[num_modes].rows > 999))) {
                continue;
            }
            text_modes[num_modes++].mode = i;
        }
        
        if (num_modes-1 < menu_bottom - menu_top) menu_bottom = menu_top + num_modes-1;
        UINTN menu_len = menu_bottom - menu_top + 1;    // 1-based offset
        
        // Highlight top menu row to start off
        cout->SetCursorPosition(cout, 0, menu_top);
        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
        printf_c16(u"Mode %d: %llux%llu",
                   text_modes[0].mode, text_modes[0].cols, text_modes[0].rows);
        
        // Print other text mode infos
        cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
        for (UINT32 i = 1; i < menu_len; i++)
            printf_c16(u"\r\nMode %d: %llux%llu",
                       text_modes[i].mode, text_modes[i].cols, text_modes[i].rows);
        
        // Get input from user
        cout->SetCursorPosition(cout, 0, menu_top);
        bool getting_input = true;
        while (getting_input) {
            UINTN current_row = cout->Mode->CursorRow;
            
            EFI_INPUT_KEY key = get_key();
            switch (key.ScanCode) {
                case SCANCODE_ESC: return EFI_SUCCESS;  // ESC Key: Go back to main menu
                    
                case SCANCODE_UP_ARROW:
                    if (current_row == menu_top && mode_index > 0) {
                        // Scroll menu up by decrementing all modes by 1
                        printf_c16(u"                    \r");  // Blank out mode text first
                        
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        mode_index--;
                        printf_c16(u"Mode %d: %dx%d",
                                   text_modes[mode_index].mode,
                                   text_modes[mode_index].cols, text_modes[mode_index].rows);
                        
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                        UINTN temp_mode = mode_index + 1;
                        for (UINT32 i = 0; i < menu_len; i++, temp_mode++) {
                            printf_c16(u"\r\n                    \r"  // Blank out mode text first
                                       u"Mode %d: %dx%d\r",
                                       text_modes[temp_mode].mode,
                                       text_modes[temp_mode].cols, text_modes[temp_mode].rows);
                        }
                        
                        // Reset cursor to top of menu
                        cout->SetCursorPosition(cout, 0, menu_top);
                        
                    } else if (current_row-1 >= menu_top) {
                        // De-highlight current row, move up 1 row, highlight new row
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   text_modes[mode_index].mode,
                                   text_modes[mode_index].cols, text_modes[mode_index].rows);
                        
                        mode_index--;
                        current_row--;
                        cout->SetCursorPosition(cout, 0, current_row);
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   text_modes[mode_index].mode,
                                   text_modes[mode_index].cols, text_modes[mode_index].rows);
                    }
                    
                    // Reset colors
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                    break;
                    
                case SCANCODE_DOWN_ARROW:
                    if (current_row == menu_bottom && mode_index < num_modes-1) {
                        // Not at bottom of modes yet, scroll menu down by incrementing all modes by 1
                        mode_index -= menu_len - 1;
                        
                        // Print modes up until the last menu row
                        cout->SetCursorPosition(cout, 0, menu_top);
                        for (UINT32 i = 0; i < menu_len; i++, mode_index++) {
                            printf_c16(u"                    \r"    // Blank out mode text first
                                       u"Mode %d: %dx%d\r\n",
                                       text_modes[mode_index].mode,
                                       text_modes[mode_index].cols, text_modes[mode_index].rows);
                        }
                        
                        // Highlight last row
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   text_modes[mode_index].mode,
                                   text_modes[mode_index].cols, text_modes[mode_index].rows);
                        
                    } else if (current_row+1 <= menu_bottom) {
                        // De-highlight current row, move down 1 row, highlight new row
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r\n",
                                   text_modes[mode_index].mode,
                                   text_modes[mode_index].cols, text_modes[mode_index].rows);
                        
                        mode_index++;
                        current_row++;
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   text_modes[mode_index].mode,
                                   text_modes[mode_index].cols, text_modes[mode_index].rows);
                    }
                    
                    // Reset colors
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                    break;
                    
                default:
                    if (key.UnicodeChar == u'\r' && text_modes[mode_index].cols != 0) {    // Qemu can have invalid text modes
                        // Enter key, set Text mode
                        cout->SetMode(cout, text_modes[mode_index].mode);
                        cout->QueryMode(cout, text_modes[mode_index].mode,
                                        &text_modes[mode_index].cols, &text_modes[mode_index].rows);
                        
                        // Set global rows/cols values
                        text_rows = text_modes[mode_index].rows;
                        text_cols = text_modes[mode_index].cols;
                        
                        cout->ClearScreen(cout);
                        
                        getting_input = false;  // Will leave input loop and redraw screen
                        mode_index = 0;         // Reset last selected mode in menu
                    }
                    break;
            }
        }
    }
    
    return EFI_SUCCESS;
}

// ====================
// Set Graphics Mode
// ====================
EFI_STATUS set_graphics_mode(void) {
    // Get GOP protocol via LocateProtocol()
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info = NULL;
    UINTN mode_info_size = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
    EFI_STATUS status = 0;
    UINTN mode_index = 0;   // Current mode within entire menu of GOP mode choices;
    
    // Store found GOP mode info
    typedef struct {
        UINT32 width;
        UINT32 height;
    } Gop_Mode_Info;
    
    Gop_Mode_Info gop_modes[50];
    
    status = bs->LocateProtocol(&gop_guid, NULL, (VOID **)&gop);
    if (EFI_ERROR(status)) {
        printf_c16(u"Could not locate GOP! :(\r\n");
        return status;
    }
    
    // Overall screen loop
    while (true) {
        cout->ClearScreen(cout);
        
        // Get current GOP mode information
        printf_c16(u"Graphics mode information:\r\n");
        status = gop->QueryMode(gop,
                                gop->Mode->Mode,
                                &mode_info_size,
                                &mode_info);
        
        if (EFI_ERROR(status)) {
            printf_c16(u"Could not Query GOP Mode %u\r\n", gop->Mode->Mode);
            return status;
        }
        
        printf_c16(u"Max Mode: %d\r\n"
                   u"Current Mode: %d\r\n"
                   u"WidthxHeight: %ux%u\r\n"
                   u"Framebuffer address: %x\r\n"
                   u"Framebuffer size: %x\r\n"
                   u"PixelFormat: %d\r\n"
                   u"PixelsPerScanLine: %u\r\n",
                   gop->Mode->MaxMode,
                   gop->Mode->Mode,
                   mode_info->HorizontalResolution, mode_info->VerticalResolution,
                   gop->Mode->FrameBufferBase,
                   gop->Mode->FrameBufferSize,
                   mode_info->PixelFormat,
                   mode_info->PixelsPerScanLine);
        
        cout->OutputString(cout, u"\r\nAvailable GOP modes:\r\n");
        
        // Get current text mode ColsxRows values
        UINTN menu_top = cout->Mode->CursorRow, menu_bottom = 0, max_cols;
        cout->QueryMode(cout, cout->Mode->Mode, &max_cols, &menu_bottom);
        
        // Print keybinds at bottom of screen
        cout->SetCursorPosition(cout, 0, menu_bottom-3);
        printf_c16(u"Up/Down Arrow = Move Cursor\r\n"
                   u"Enter = Select\r\n"
                   u"Escape = Go Back");
        
        cout->SetCursorPosition(cout, 0, menu_top);
        menu_bottom -= 5;   // Bottom of menu will be 2 rows above keybinds
        UINTN menu_len = menu_bottom - menu_top;
        
        // Get all available GOP modes' info
        const UINT32 max = gop->Mode->MaxMode;
        if (max < menu_len) {
            // Bound menu by actual # of available modes
            menu_bottom = menu_top + max-1;
            menu_len = menu_bottom - menu_top;  // Limit # of modes in menu to max mode - 1
        }
        
        for (UINT32 i = 0; i < ARRAY_SIZE(gop_modes) && i < max; i++) {
            gop->QueryMode(gop, i, &mode_info_size, &mode_info);
            
            gop_modes[i].width = mode_info->HorizontalResolution;
            gop_modes[i].height = mode_info->VerticalResolution;
        }
        
        // Highlight top menu row to start off
        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
        printf_c16(u"Mode %d: %dx%d", 0, gop_modes[0].width, gop_modes[0].height);
        
        // Print other text mode infos
        cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
        for (UINT32 i = 1; i < menu_len + 1; i++)
            printf_c16(u"\r\nMode %d: %dx%d", i, gop_modes[i].width, gop_modes[i].height);
        
        // Get input from user
        cout->SetCursorPosition(cout, 0, menu_top);
        bool getting_input = true;
        while (getting_input) {
            UINTN current_row = cout->Mode->CursorRow;
            
            EFI_INPUT_KEY key = get_key();
            switch (key.ScanCode) {
                case SCANCODE_ESC: return EFI_SUCCESS;  // ESC Key: Go back to main menu
                    
                case SCANCODE_UP_ARROW:
                    if (current_row == menu_top && mode_index > 0) {
                        // Scroll menu up by decrementing all modes by 1
                        printf_c16(u"                    \r");  // Blank out mode text first
                        
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        mode_index--;
                        printf_c16(u"Mode %d: %dx%d",
                                   mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                        
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                        UINTN temp_mode = mode_index + 1;
                        for (UINT32 i = 0; i < menu_len; i++, temp_mode++) {
                            printf_c16(u"\r\n                    \r"  // Blank out mode text first
                                       u"Mode %d: %dx%d\r",
                                       temp_mode, gop_modes[temp_mode].width, gop_modes[temp_mode].height);
                        }
                        
                        // Reset cursor to top of menu
                        cout->SetCursorPosition(cout, 0, menu_top);
                        
                    } else if (current_row-1 >= menu_top) {
                        // De-highlight current row, move up 1 row, highlight new row
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                        
                        mode_index--;
                        current_row--;
                        cout->SetCursorPosition(cout, 0, current_row);
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                    }
                    
                    // Reset colors
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                    break;
                    
                case SCANCODE_DOWN_ARROW:
                    // NOTE: Max valid GOP mode is ModeMax-1 per UEFI spec
                    if (current_row == menu_bottom && mode_index < max-1) {
                        // Scroll menu down by incrementing all modes by 1
                        mode_index -= menu_len - 1;
                        
                        // Reset cursor to top of menu
                        cout->SetCursorPosition(cout, 0, menu_top);
                        
                        // Print modes up until the last menu row
                        for (UINT32 i = 0; i < menu_len; i++, mode_index++) {
                            printf_c16(u"                    \r"    // Blank out mode text first
                                       u"Mode %d: %dx%d\r\n",
                                       mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                        }
                        
                        // Highlight last row
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                        
                    } else if (current_row+1 <= menu_bottom) {
                        // De-highlight current row, move down 1 row, highlight new row
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r\n",
                                   mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                        
                        mode_index++;
                        current_row++;
                        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                        printf_c16(u"                    \r"    // Blank out mode text first
                                   u"Mode %d: %dx%d\r",
                                   mode_index, gop_modes[mode_index].width, gop_modes[mode_index].height);
                    }
                    
                    // Reset colors
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                    break;
                    
                default:
                    if (key.UnicodeChar == u'\r') {
                        // Enter key, set GOP mode
                        gop->SetMode(gop, mode_index);
                        gop->QueryMode(gop, mode_index, &mode_info_size, &mode_info);
                        
                        // Clear GOP screen
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = px_BLUE;
                        gop->Blt(gop, &px, EfiBltVideoFill,
                                 0, 0,  // Origin BLT BUFFER X,Y
                                 0, 0,  // Destination screen X,Y
                                 mode_info->HorizontalResolution, mode_info->VerticalResolution,
                                 0);
                        
                        getting_input = false;  // Will leave input loop and redraw screen
                        mode_index = 0;         // Reset last selected mode in menu
                    }
                    break;
            }
        }
    }
    
    return EFI_SUCCESS;
}


EFI_STATUS test_mouse(void){
    EFI_GUID spp_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID ;
    EFI_SIMPLE_POINTER_PROTOCOL *spp = NULL;
    UINTN num_handles = 0;
    EFI_HANDLE *handle_buffer = NULL;
    EFI_STATUS status = 0;
    // Store found GOP mode info
    
    
    status = bs->LocateHandleBuffer(ByProtocol, &spp_guid, NULL, &num_handles, (VOID **)&handle_buffer);
    
    if (EFI_ERROR(status)){
        printf_c16(u"\r\nERROR: %x; Could not local GOP! :(\r\n", status);
        return status;
    }
    
    cout->ClearScreen(cout);
    bool found_mode = false;
    for (UINTN i = 0; i < num_handles; i++) {
        status = bs->OpenProtocol(handle_buffer[i],
                                  &spp_guid,
                                  (VOID**)&spp,
                                  image,
                                  NULL,
                                  EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        
        if (EFI_ERROR(status)){
            printf_c16(u"\r\nERROR: %x, Could not locate Simple Pointer Protocol on handle.",status);
            return status;
        }
        
        printf_c16(u"SPP %u; Resolution X: %u, Y: %u, Z:%u, LButton: %b, RButton: %b \r\n",
                   i,
                   spp->Mode->ResolutionX,
                   spp->Mode->ResolutionY,
                   spp->Mode->ResolutionZ,
                   spp->Mode->LeftButton,
                   spp->Mode->RightButton);
        
        
        if (spp->Mode->ResolutionX < 65536){
            found_mode = true;
        }
        
    }
    
    if (!found_mode){
        printf_c16(u"\r\nError: Could not find any valid SPP Mode.\r\n");
        get_key();
        return 1;
    }
    
    get_key();
    
    while(1);
    
    return EFI_SUCCESS;
}

CHAR16* strcpy_u16(CHAR16* dst, CHAR16* src){
    if (!dst) return NULL;
    if (!src) return dst;
    
    CHAR16 *result = dst;
    while (*src) *dst++ = *src++;
    
    *dst = u'\0';
    
    return result;
}

CHAR16* strchr_u16(CHAR16* str, CHAR16 c){
    
    CHAR16 *result = NULL;
    while (*str) {
        if (*str == c) result = str;
        str++;
    }
    
    
    return result;
}

EFI_STATUS read_esp_files(void){
    EFI_STATUS status = EFI_SUCCESS;
    
    // Get ESP root directory
    EFI_FILE_PROTOCOL *dirp = esp_root_dir();
    if (!dirp) {
        error(0, u"Could not get ESP root directory.\r\n");
        goto done;
    }
    
    // Start at root directory
    CHAR16 current_directory[256];
    strcpy_c16(current_directory, u"/");
    
    // Print dir entries for currently opened directory
    // Overall input loop
    INT32 csr_row = 1;
    while (true) {
        cout->ClearScreen(cout);
        printf_c16(u"%s:\r\n", current_directory);
        
        INT32 num_entries = 0;
        EFI_FILE_INFO file_info;
        
        dirp->SetPosition(dirp, 0);                 // Reset to start of directory entries
        UINTN buf_size = sizeof file_info;
        dirp->Read(dirp, &buf_size, &file_info);
        while (buf_size > 0) {
            num_entries++;
            
            // Got next dir entry, print info
            if (csr_row == cout->Mode->CursorRow) {
                // Highlight row cursor/user is on
                cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
            }
            
            printf_c16(u"%s %s\r\n",
                       (file_info.Attribute & EFI_FILE_DIRECTORY) ? u"[DIR] " : u"[FILE]",
                       file_info.FileName);
            
            if (csr_row+1 == cout->Mode->CursorRow) {
                // De-highlight rows after cursor
                cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
            }
            
            buf_size = sizeof file_info;
            dirp->Read(dirp, &buf_size, &file_info);
        }
        
        EFI_INPUT_KEY key = get_key();
        switch (key.ScanCode) {
            case SCANCODE_ESC:
                // ESC Key, exit and go back to main menu
                goto done;
                break;
                
            case SCANCODE_UP_ARROW:
            case SCANCODE_DOWN_ARROW:
                // Go up or down 1 row in range [1:num_entries] (circular buffer)
                csr_row = (key.ScanCode == SCANCODE_UP_ARROW)
                ? ((csr_row-1 + num_entries-1) % num_entries) + 1
                : (csr_row % num_entries) + 1;
                break;
                
            default:
                if (key.UnicodeChar == u'\r') {
                    // Enter key:
                    //   for a directory, enter that directory and iterate the loop
                    //   for a file, print the file contents to screen
                    
                    // Get directory entry under user cursor row
                    dirp->SetPosition(dirp, 0);  // Reset to start of directory entries
                    INT32 i = 0;
                    do {
                        buf_size = sizeof file_info;
                        dirp->Read(dirp, &buf_size, &file_info);
                        i++;
                    } while (i < csr_row);
                    
                    if (file_info.Attribute & EFI_FILE_DIRECTORY) {
                        // Directory, open and enter this new directory
                        EFI_FILE_PROTOCOL *new_dir;
                        status = dirp->Open(dirp,
                                            &new_dir,
                                            file_info.FileName,
                                            EFI_FILE_MODE_READ,
                                            0);
                        
                        if (EFI_ERROR(status)) {
                            error(status, u"Could not open new directory %s\r\n", file_info.FileName);
                            goto done;
                        }
                        
                        dirp->Close(dirp);  // Close last opened dir
                        dirp = new_dir;     // Set new opened dir
                        csr_row = 1;        // Reset user row to first entry in new directory
                        
                        // Set new path for current directory
                        if (!strncmp_u16(file_info.FileName, u".", 2)) {
                            // Current directory, do nothing
                            
                        } else if (!strncmp_u16(file_info.FileName, u"..", 3)) {
                            // Parent directory, go back up and remove dir name from path
                            CHAR16 *pos = strrchr_u16(current_directory, u'/');
                            if (pos == current_directory) pos++;    // Move past initial root dir '/'
                            
                            *pos = u'\0';
                            
                        } else {
                            // Go into nested directory, add on to current string
                            if (current_directory[1] != u'\0') {
                                strcat_c16(current_directory, u"/");
                            }
                            strcat_c16(current_directory, file_info.FileName);
                        }
                        continue;   // Continue overall loop and print new directory entries
                    }
                    
                    // Else this is a file, print contents:
                    // Allocate buffer for file
                    VOID *buffer = NULL;
                    buf_size = file_info.FileSize;
                    status = bs->AllocatePool(EfiLoaderData, buf_size, &buffer);
                    if (EFI_ERROR(status)) {
                        error(status, u"Could not allocate memory for file %s\r\n", file_info.FileName);
                        goto done;
                    }
                    
                    // Open file
                    EFI_FILE_PROTOCOL *file = NULL;
                    status = dirp->Open(dirp,
                                        &file,
                                        file_info.FileName,
                                        EFI_FILE_MODE_READ,
                                        0);
                    
                    if (EFI_ERROR(status)) {
                        error(status, u"Could not open file %s\r\n", file_info.FileName);
                        goto done;
                    }
                    
                    // Read file into buffer
                    status = dirp->Read(file, &buf_size, buffer);
                    if (EFI_ERROR(status)) {
                        error(status, u"Could not read file %s into buffer.\r\n", file_info.FileName);
                        goto done;
                    }
                    
                    if (buf_size != file_info.FileSize) {
                        error(0, u"Could not read all of file %s into buffer.\r\n"
                              u"Bytes read: %u, Expected: %u\r\n",
                              file_info.FileName, buf_size, file_info.FileSize);
                        goto done;
                    }
                    
                    // Print buffer contents
                    printf_c16(u"\r\nFile Contents:\r\n");
                    
                    char *pos = (char *)buffer;
                    for (UINTN bytes = buf_size; bytes > 0; bytes--) {
                        CHAR16 str[2];
                        str[0] = *pos;
                        str[1] = u'\0';
                        if (*pos == '\n') {
                            // Convert LF newline to CRLF
                            printf_c16(u"\r\n");
                        } else {
                            printf_c16(u"%s", str);
                        }
                        
                        pos++;
                    }
                    
                    printf_c16(u"\r\n\r\nPress any key to continue...\r\n");
                    get_key();
                    
                    // Free memory for file when done
                    bs->FreePool(buffer);
                    
                    // Close file handle
                    dirp->Close(file);
                }
                break;
        }
    }
    
done:
    if (dirp) dirp->Close(dirp);    // Cleanup directory pointer
    return status;
}

EFI_STATUS print_block_io_partitions(void){
    EFI_STATUS status = EFI_SUCCESS;
    
    cout->ClearScreen(cout);
    
    EFI_GUID bio_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_BLOCK_IO_PROTOCOL *biop;
    UINTN num_handles = 0;
    EFI_HANDLE *handle_buffer = NULL;
    
    // Get media ID for this disk image first, to compare to others in output
    UINT32 this_image_media_id = 0;
    status = get_disk_image_mediaID(&this_image_media_id);
    if (EFI_ERROR(status)) {
        printf_c16(u"Could not get disk image media ID.\r\n");
        return status;
    }
    
    // Loop through and print all partition information found
    status = bs->LocateHandleBuffer(ByProtocol, &bio_guid, NULL, &num_handles, &handle_buffer);
    if (EFI_ERROR(status)) {
        printf_c16(u"Could not locate any Block IO Protocols.\r\n");
        return status;
    }
    
    UINT32 last_media_id = -1;  // Keep track of currently opened Media info
    for (UINTN i = 0; i < num_handles; i++) {
        status = bs->OpenProtocol(handle_buffer[i],
                                  &bio_guid,
                                  (VOID **)&biop,
                                  image,
                                  NULL,
                                  EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        
        if (EFI_ERROR(status)) {
            printf_c16(u"Could not Open Block IO protocol on handle %u.\r\n", i);
            continue;
        }
        
        // Print Block IO Media Info for this Disk/partition
        if (last_media_id != biop->Media->MediaId) {
            last_media_id = biop->Media->MediaId;
            printf_c16(u"Media ID: %u %s\r\n",
                       last_media_id,
                       (last_media_id == this_image_media_id ? u"(Disk Image)" : u""));
        }
        
        if (biop->Media->LastBlock == 0) {
            // Only really care about partitions/disks above 1 block in size
            continue;
        }
        
        printf_c16(u"Rmv: %s, Pr: %s, LglPrt: %s, RdOnly: %s, Wrt$: %s\r\n"
                   u"BlkSz: %u, IoAln: %u, LstBlk: %u, LwLBA: %u, LglBlkPerPhys: %u\r\n"
                   u"OptTrnLenGran: %u\r\n",
                   biop->Media->RemovableMedia   ? u"Y" : u"N",
                   biop->Media->MediaPresent     ? u"Y" : u"N",
                   biop->Media->LogicalPartition ? u"Y" : u"N",
                   biop->Media->ReadOnly         ? u"Y" : u"N",
                   biop->Media->WriteCaching     ? u"Y" : u"N",
                   
                   biop->Media->BlockSize,
                   biop->Media->IoAlign,
                   biop->Media->LastBlock,
                   biop->Media->LowestAlignedLba,
                   biop->Media->LogicalBlocksPerPhysicalBlock,
                   biop->Media->OptimalTransferLengthGranularity);
        
        // Print type of partition e.g. ESP or Data or Other
        if (!biop->Media->LogicalPartition) printf_c16(u"<Entire Disk>\r\n");
        else {
            // Get partition info protocol for this partition
            EFI_GUID pi_guid = EFI_PARTITION_INFO_PROTOCOL_GUID;
            EFI_PARTITION_INFO_PROTOCOL *pip = NULL;
            status = bs->OpenProtocol(handle_buffer[i],
                                      &pi_guid,
                                      (VOID **)&pip,
                                      image,
                                      NULL,
                                      EFI_OPEN_PROTOCOL_GET_PROTOCOL);
            
            if (EFI_ERROR(status)) {
                printf_c16(u"Could not Open Partition Info protocol on handle %u.\r\n", i);
            } else {
                if      (pip->Type == PARTITION_TYPE_OTHER) printf_c16(u"<Other Type>\r\n");
                else if (pip->Type == PARTITION_TYPE_MBR)   printf_c16(u"<MBR>\r\n");
                else if (pip->Type == PARTITION_TYPE_GPT) {
                    if (pip->System == 1) printf_c16(u"<EFI System Partition>\r\n");
                    else {
                        // Compare Gpt.PartitionTypeGUID with known values
                        EFI_GUID data_guid = BASIC_DATA_GUID;
                        if (!memcmp(&pip->Info.Gpt.PartitionTypeGUID, &data_guid, sizeof(EFI_GUID)))
                            printf_c16(u"<Basic Data>\r\n");
                        else
                            printf_c16(u"<Other GPT Type>\r\n");
                    }
                }
            }
        }
        
        printf_c16(u"\r\n");    // Separate each block of text visually
    }
    
    printf_c16(u"Press any key to go back..\r\n");
    get_key();
    return EFI_SUCCESS;
}

EFI_STATUS read_data_partition_file(void){
    cout->ClearScreen(cout);
    
    CHAR16* file_name = u"\\EFI\\BOOT\\DATAFLS.INF";
    UINTN buf_size = 0;
    VOID* file_buffer = read_esp_file_to_buffer(file_name,&buf_size);
    
    if (!file_buffer){
        printf_c16(u"Could not find or read file '%s' to buffer\r\n",file_name);
        return 1;
    }
    
    printf_c16(u"HERE\r\n");
    get_key();
    return EFI_SUCCESS;
}

// =========================================================================
// Load an ELF64 PIE file into a new buffer, and return the
// entry point for the loaded ELF program
// =========================================================================
VOID *load_elf(VOID *elf_buffer, EFI_PHYSICAL_ADDRESS *kernel_buffer, UINTN *kernel_size) {
    printf_c16(u"ELF64 PIE, Not implemented yet...\r\n");
    
    ELF_Header_64 *ehdr = elf_buffer;
    
    // Print elf header info for user
    printf_c16(u"Type: %u, Machine: %x, Entry: %x\r\n"
               u"Pgm headers offset: %u, Elf Header Size: %u\r\n"
               u"Pgm entry size: %u, # of Pgm headers: %u\r\n",
               ehdr->e_type, ehdr->e_machine, ehdr->e_entry,
               ehdr->e_phoff, ehdr->e_ehsize,
               ehdr->e_phentsize, ehdr->e_phnum);
    
    if (ehdr->e_type != ET_DYN) {
        printf_c16(u"ELF is not a PIE file; e_type is not ETDYN/0x03\r\n");
        return NULL;
    }
    
    ELF_Program_Header_64 *phdr = (ELF_Program_Header_64 *)((UINT8 *)ehdr + ehdr->e_phoff);
    printf_c16(u"Loadable Program Headers:\r\n");
    
    UINTN max_alignment = PAGE_SIZE;
    UINTN mem_min = UINT64_MAX, mem_max = 0;
    
    for (UINT16 i = 0; i < ehdr->e_phnum; i++, phdr = (ELF_Program_Header_64 *)((UINT8 *)phdr + ehdr->e_phentsize)) {
        if (phdr->p_type != PT_LOAD) continue;
        
        printf_c16(u"%u: Offset: %x, Vaddr: %x, Paddr: %x, FileSize: %x\r\n"
                   u"    MemSize: %x, Alignment: %x\r\n",
                   (UINTN)i, phdr->p_offset, phdr->p_vaddr, phdr->p_paddr,
                   phdr->p_filesz, phdr->p_memsz, phdr->p_align);
        
        if (max_alignment < phdr->p_align) max_alignment = phdr->p_align;
        
        UINTN mem_begin = phdr->p_vaddr;
        UINTN mem_end   = phdr->p_vaddr + phdr->p_memsz;
        
        mem_begin &= ~(max_alignment-1);
        mem_end = (mem_end + max_alignment-1) & ~(max_alignment-1);
        
        if (mem_begin < mem_min) mem_min = mem_begin;
        if (mem_end > mem_max) mem_max = mem_end;
    }
    
    if (mem_min == UINT64_MAX || mem_max == 0 || mem_max <= mem_min) {
        printf_c16(u"No loadable segments found or invalid memory range\r\n");
        return NULL;
    }
    
    UINTN max_memory_needed = mem_max - mem_min;
    
    printf_c16(u"\r\nMemory needed for file: %x\r\n", max_memory_needed);
    
    EFI_STATUS status = EFI_SUCCESS;
    EFI_PHYSICAL_ADDRESS program_buffer = 0;
    UINTN pages_needed = (max_memory_needed + (PAGE_SIZE - 1)) / PAGE_SIZE;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderCode, pages_needed, &program_buffer);
    if (EFI_ERROR(status)) {
        printf_c16(u"Error %x; Could not allocate memory for ELF program\r\n", status);
        return NULL;
    }
    
    // Initialize buffer to zeros
    memset((VOID *)program_buffer, 0, max_memory_needed);
    
    *kernel_buffer = program_buffer;
    *kernel_size = pages_needed * PAGE_SIZE;
    
    // Second pass: Load program segments into the allocated buffer
    phdr = (ELF_Program_Header_64 *)((UINT8 *)ehdr + ehdr->e_phoff);
    
    for (UINT16 i = 0; i < ehdr->e_phnum; i++, phdr = (ELF_Program_Header_64 *)((UINT8 *)phdr + ehdr->e_phentsize)) {
        if (phdr->p_type != PT_LOAD) continue;
        
        // Calculate relative offset from mem_min
        UINTN relative_offset = phdr->p_vaddr - mem_min;
        
        // Destination in the newly allocated program buffer
        UINT8 *dst = (UINT8 *)program_buffer + relative_offset;
        // Source in the original ELF file buffer
        UINT8 *src = (UINT8 *)elf_buffer + phdr->p_offset;
        
        UINT32 len = phdr->p_filesz;
        // Copy p_filesz bytes from the ELF file to the program buffer
        if (phdr->p_filesz > 0) {
            memcpy(dst, src, len);
            printf_c16(u"MEMCPY dst: %x, src: %x, len: %x\r\n", (UINTN)dst, (UINTN)src, phdr->p_filesz);
        }
    }
    
    // Calculate entry point relative to program_buffer
    VOID *entry_point = (VOID *)((UINT8 *)program_buffer + (ehdr->e_entry - mem_min));
    
    printf_c16(u"ELF loaded. Entry point: %x\r\n", (UINTN)entry_point);
    
    return entry_point;
}

EFI_STATUS get_memory_map(Memory_Map_Info *mmap){
    EFI_STATUS status = EFI_SUCCESS;
    memset(mmap, 0, sizeof *mmap);
    status = bs->GetMemoryMap(&mmap->size,
                              mmap->map,
                              &mmap->key,
                              &mmap->desc_size,
                              &mmap->desc_version);
    
    if (EFI_ERROR(status) && status != EFI_BUFFER_TOO_SMALL){
        printf_c16(u"Could not find or read data partition file to buffer\r\n");
        return status;
    }
    
    mmap->size += mmap->desc_size * 2;
    status = bs->AllocatePool(EfiLoaderData, mmap->size, (VOID**)&mmap->map);
    
    if (EFI_ERROR(status)){
        printf_c16(u"Error:%x,Could not allocate buffer for memory map\r\n",status);
        return status;
    }
    
    status = bs->GetMemoryMap(&mmap->size,
                              mmap->map,
                              &mmap->key,
                              &mmap->desc_size,
                              &mmap->desc_version);
    
    if (EFI_ERROR(status)){
        printf_c16(u"Error %x,Could not get UEFI memory map!\r\n",status);
        return status;
    }
    
    
    return EFI_SUCCESS;
}

EFI_STATUS print_memory_map_with(Memory_Map_Info mmap){
    cout->ClearScreen(cout);
    
    
    printf_c16(u"EFI_MEMORY_DESCRIPTOR size: %u\r\n",sizeof(EFI_MEMORY_DESCRIPTOR));
    
    printf_c16(u"Memory map: Size %u, Descriptor size: %u, # of descriptors: %u, key: %x\r\n",
               mmap.size, mmap.desc_size, mmap.size / mmap.desc_size, mmap.key);
    
    UINT32 usable_bytes = 0;
    for (UINTN i = 0; i < mmap.size / mmap.desc_size; i++){
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8*)mmap.map + (i * mmap.desc_size));
        printf_c16(u"%u: Typ: %u, Phy: %x, Vrt: %x, Pgs: %u, Att: %x\r\n",
                   i,
                   desc->Type,
                   desc->PhysicalStart,
                   desc->VirtualStart,
                   desc->NumberOfPages,
                   desc->Attribute);
        
        if (desc->Type == EfiLoaderCode         ||
            desc->Type == EfiLoaderData         ||
            desc->Type == EfiBootServicesCode   ||
            desc->Type == EfiBootServicesData   ||
            desc->Type == EfiConventionalMemory ||
            desc->Type == EfiPersistentMemory) {
            
            usable_bytes += desc->NumberOfPages * 4096;
        }
        if (i > 0 && i % 20 == 0){
            get_key();
        }
    }
    
    printf_c16(u"\r\nUsable memory: %u / %u MiB / %u GiB\r\n",
               usable_bytes,
               usable_bytes / (1024 * 1024),
               usable_bytes / (1024 * 1024 * 1024));
    
    
    printf_c16(u"\r\nPress any key to go back...\r\n");
    get_key();
    return EFI_SUCCESS;
}

EFI_STATUS print_memory_map(void){
    
    Memory_Map_Info mmap;
    get_memory_map(&mmap);
    
    return print_memory_map_with(mmap);
}

void* mmap_allocate_pages2(Memory_Map_Info *mmap, UINTN pages) {
    static void *next_page_address = NULL;  // Next page/page range address to return to caller
    static UINTN current_descriptor = 0;    // Current descriptor number
    static UINTN remaining_pages = 0;       // Remaining pages in current descriptor
    
    if (remaining_pages < pages) {
        // Not enough remaining pages in current descriptor, find the next available one
        UINTN i = current_descriptor + 1;
        for (; i < mmap->size / mmap->desc_size; i++) {
            EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *) ((UINT8 *)mmap->map + (i * mmap->desc_size));
            
            if (desc->Type == EfiConventionalMemory && desc->NumberOfPages >= pages) {
                // Found enough memory to use at this descriptor, use it
                current_descriptor = i;
                remaining_pages = desc->NumberOfPages - pages;
                next_page_address = (void *)(desc->PhysicalStart + pages * PAGE_SIZE);
                return (void *) desc->PhysicalStart;
            }
        }
        
        if (i >= mmap->size / mmap->desc_size) {
            // Ran out of descriptors to check in memory map
            printf_c16(u"\r\nERROR: Could not find any memory to allocate pages for.\r\n");
            return NULL;
        }
    }
    
    // Else we have at least enough pages for this allocation, return the current spot in the memory map
    remaining_pages -= pages;
    void *page = next_page_address;
    next_page_address = (void *)((UINT8 *)page + (pages * PAGE_SIZE));
    return page;
}

bool map_page(UINTN physical_address, UINTN virtual_address, Memory_Map_Info *mmap){
    int flags = PRESENT | READWRITE | USER;    // 0b111
    
    UINTN pml4_index = ((virtual_address) >> 39) & 0x1FF;
    UINTN pdpt_index = ((virtual_address) >> 30) & 0x1FF;
    UINTN pdt_index  = ((virtual_address) >> 21) & 0x1FF;
    UINTN pt_index   = ((virtual_address) >> 12) & 0x1FF;
    
    // Make sure pdpt exists, if not then allocate it
    if (!(pml4->entries[pml4_index] & PRESENT)) {
        void *pdpt_address = mmap_allocate_pages2(mmap, 1);
        
        memset(pdpt_address, 0, sizeof(Page_Table));
        pml4->entries[pml4_index] = (UINTN)pdpt_address | flags;
    }
    
    Page_Table *pdpt = (Page_Table*)(pml4->entries[pml4_index] & PHYS_PAGE_ADDR_MASK);
    if (!(pdpt->entries[pdpt_index] & PRESENT)){
        void *pdt_address = mmap_allocate_pages2(mmap, 1);
        
        memset(pdt_address, 0, sizeof(Page_Table));
        pdpt->entries[pdpt_index] = (UINTN)pdt_address | flags;
    }
    
    Page_Table *pdt = (Page_Table*)(pdpt->entries[pml4_index] & PHYS_PAGE_ADDR_MASK);
    if (!(pdt->entries[pdt_index] & PRESENT)){
        void *pt_address = mmap_allocate_pages2(mmap, 1);
        
        memset(pt_address, 0, sizeof(Page_Table));
        pdt->entries[pdt_index] = (UINTN)pt_address | flags;
    }
    
    Page_Table *pt = (Page_Table*)(pdt->entries[pml4_index] & PHYS_PAGE_ADDR_MASK);
    if (!(pt->entries[pt_index] & PRESENT)) {
        pt->entries[pt_index] = (physical_address & PHYS_PAGE_ADDR_MASK) | flags;
    }
}

void unmap_page(UINTN virtual_address, Memory_Map_Info *mmap) {
    UINTN pml4_index = ((virtual_address) >> 39) & 0x1FF;
    UINTN pdpt_index = ((virtual_address) >> 30) & 0x1FF;
    UINTN pdt_index  = ((virtual_address) >> 21) & 0x1FF;
    UINTN pt_index   = ((virtual_address) >> 12) & 0x1FF;
    
    Page_Table *pdpt = (Page_Table*)(pml4->entries[pml4_index] & PHYS_PAGE_ADDR_MASK);
    Page_Table *pdt = (Page_Table*)(pdpt->entries[pml4_index] & PHYS_PAGE_ADDR_MASK);
    Page_Table *pt = (Page_Table*)(pdt->entries[pml4_index] & PHYS_PAGE_ADDR_MASK);
    
    pt->entries[pt_index] = 0;
    
    __asm__ __volatile__("invlpg (%0)\n" : : "r"(virtual_address));
}

void identiy_map_efi_mmap(Memory_Map_Info *mmap){
    for (UINTN i = 0; i < mmap->size / mmap->desc_size; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mmap->map + (i * mmap->desc_size));
        
        for (UINTN j = 0; j < desc->NumberOfPages; j++) {
            identity_map_page(desc->PhysicalStart + (j * PAGE_SIZE), mmap);
        }
    }
}

void set_runtime_address_map2(Memory_Map_Info* mmap){
    UINTN runtime_descriptors = 0;
    for (UINTN i = 0; i < mmap->size / mmap->desc_size; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mmap->map + (i * mmap->desc_size));
        
        if (desc->Attribute & EFI_MEMORY_RUNTIME) {
            runtime_descriptors++;
        }
    }
    
    UINTN runtime_mmap_pages = (runtime_descriptors * mmap->desc_size) + ((PAGE_SIZE -1) / PAGE_SIZE);
    EFI_MEMORY_DESCRIPTOR *runtime_mmap = mmap_allocate_pages(mmap, runtime_mmap_pages);
    if (!runtime_mmap){
        printf_c16(u"ERROR: could not allocate runtime descriptors memory map\r\n");
        return;
    }
    
    UINTN runtime_mmap_size = runtime_mmap_pages * PAGE_SIZE;
    memset(runtime_mmap, 0, runtime_mmap_size);
    
    
    UINTN curr_runtime_desc = 0;
    for (UINTN i = 0; i < mmap->size / mmap->desc_size; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8*)mmap->map + (i * mmap->desc_size));
        
        if (desc->Attribute & EFI_MEMORY_RUNTIME) {
            EFI_MEMORY_DESCRIPTOR *runtime_desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)runtime_mmap + (curr_runtime_desc * mmap->desc_size));
            
            memcpy(runtime_desc, desc, sizeof *desc);
            runtime_desc->VirtualStart = runtime_desc->PhysicalStart;
            curr_runtime_desc++;
        }
    }
    
    
    EFI_STATUS status = rs->SetVirtualAddressMap(runtime_mmap_size,
                                                 mmap->desc_size,
                                                 mmap->desc_version,
                                                 runtime_mmap);
    
    if (EFI_ERROR(status)){
        printf_c16(u"ERROR: SetVirtualAddressMap()\r\n");
    }
}

EFI_STATUS load_kernel(void) {
    VOID *file_buffer = NULL;
    VOID *disk_buffer = NULL;
    EFI_STATUS status = EFI_SUCCESS;
    
    // Print file info for DATAFLS.INF file from path "/EFI/BOOT/DATAFLS.INF"
    CHAR16 *file_name = u"\\EFI\\BOOT\\DATAFLS.INF";
    
    cout->ClearScreen(cout);
    
    // Read file into buffer
    UINTN buf_size = 0;
    file_buffer = read_esp_file_to_buffer(file_name, &buf_size);
    if (!file_buffer) {
        printf_c16(u"Could not find or read file '%s' to buffer\r\n", file_name);
        goto exit;
    }
    
    // Parse data from DATAFLS.INF file to get disk LBA and file size
    char *str_pos = NULL;
    str_pos = strstr(file_buffer, "kernel");
    if (!str_pos) {
        printf_c16(u"Could not find kernel file in data partition\r\n");
        goto cleanup;
    }
    printf_c16(u"Found kernel file\r\n");
    
    str_pos = strstr(file_buffer, "FILE_SIZE=");
    if (!str_pos) {
        printf_c16(u"Could not find file size from buffer for '%s'\r\n", file_name);
        goto cleanup;
    }
    
    // TODO: Use an atoi function here instead?
    str_pos += strlen("FILE_SIZE=");
    UINTN file_size = 0;
    while (isdigit(*str_pos)) {
        // Convert char -> int, add next decimal digit to number
        file_size = file_size * 10 + *str_pos - '0';
        str_pos++;
    }
    
    str_pos = strstr(file_buffer, "DISK_LBA=");
    if (!str_pos) {
        printf_c16(u"Could not find disk lba value from buffer for '%s'\r\n", file_name);
        goto cleanup;
    }
    
    str_pos += strlen("DISK_LBA=");
    UINTN disk_lba = 0;
    while (isdigit(*str_pos)) {
        // Convert char -> int, add next decimal digit to number
        disk_lba = disk_lba * 10 + *str_pos - '0';
        str_pos++;
    }
    
    printf_c16(u"File Size: %u, Disk LBA: %u\r\n", file_size, disk_lba);
    
    // Get media ID (disk number for Block IO protocol Media) for this running disk image
    UINT32 image_mediaID = 0;
    status = get_disk_image_mediaID(&image_mediaID);
    if (EFI_ERROR(status)) {
        printf_c16(u"Error: %x; Could not find or get MediaID value for disk image\r\n", status);
        bs->FreePool(file_buffer); // Free memory allocated for ESP file
        goto exit;
    }
    
    // Read disk lbas for file into buffer
    disk_buffer = (VOID *)read_disk_lbas_to_buffer(disk_lba, file_size, image_mediaID, false);
    if (!disk_buffer) {
        printf_c16(u"Could not find or read data partition file to buffer\r\n");
        bs->FreePool(file_buffer); // Free memory allocated for ESP file
        goto exit;
    }
    
    
    typedef struct {
        Memory_Map_Info mmap;
        EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE gop_mode;
        EFI_RUNTIME_SERVICES              *RuntimeServices;
        UINTN                             NumberOfTableEntries;
        EFI_CONFIGURATION_TABLE           *ConfigurationTable;
    } Kernel_Params;
    
    
    Kernel_Params kparams = {
        .mmap                 = {0},
        .gop_mode             = {0},
        .RuntimeServices      = rs,
        .NumberOfTableEntries = st->NumberOfTableEntries,
        .ConfigurationTable   = st->ConfigurationTable,
    };
    
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    
    status = bs->LocateProtocol(&gop_guid, NULL, (VOID**)&gop);
    if (EFI_ERROR(status)){
        printf_c16(u"\r\nERROR: %x; Could not locate GOP! :(\r\n", status);
        return status;
    }
    
    kparams.gop_mode = *gop->Mode;
    void EFIAPI (*entry_point)(Kernel_Params) = NULL;
    
    printf_c16(u"File Format::");
    
    UINT8 *hdr = disk_buffer;
    
    printf_c16(u"Header bytes: [%x][%x][%x][%x]\r\n",hdr[0],hdr[1],hdr[2],hdr[3]);
    
    EFI_PHYSICAL_ADDRESS kernel_buffer = 0;
    UINTN kernel_size = 0;
    
    if (!memcmp(hdr, (UINT8[4]){0x7F, 'E', 'L', 'F'}, 4)){
        printf_c16(u"ELF64 PIE, Not implementd yet...\r\n");
        entry_point = (void EFIAPI (*)(Kernel_Params))load_elf(disk_buffer, &kernel_buffer, &kernel_size);
    }else if (!memcmp(hdr, (UINT8[2]){'M', 'Z'}, 2)){
        printf_c16(u"PE32+ PIE, Not implementd yet...\r\n");
    }else{
        printf_c16(u"No header bytes, Assuming it's a flat binary file\r\n");
        // Flat binary executable code assumed to start at the beginning of the loaded buffer
        entry_point = (void EFIAPI (*)(Kernel_Params))disk_buffer;
        kernel_buffer = (EFI_PHYSICAL_ADDRESS)disk_buffer;
        kernel_size = file_size;
    }
    
    
    UINTN entry_offset = (UINTN)entry_point - kernel_buffer;
    
    typedef void EFIAPI (*Entry_Point)(Kernel_Params *);
    
    Entry_Point higher_entry_point = (Entry_Point)(KERNEL_START_ADDRESS + entry_offset);
    
    
    // DEBUGGING
    printf_c16(u"\r\nOriginal Kernel address: %llx, size: %llu, entry point: %llx\r\n"
               u"Higher address entry point: %llx\r\n",
               (UINT64)kernel_buffer, (UINT64)kernel_size, (UINT64)entry_point, (UINT64)higher_entry_point);
    
    if (!entry_point) {
        bs->FreePages(kernel_buffer, kernel_size / PAGE_SIZE);
        goto cleanup;
    }
    
    // TODO: Load Kernel File depending on format (initial header bytes)
    printf_c16(u"Press any key to load kernel...\r\n");
    get_key();
    
    
    bs->CloseEvent(timer_event);
    
#define USE_MEMORY_MAP
    
#ifdef USE_MEMORY_MAP
    
    
    // Get Memory Map
    if (EFI_ERROR(get_memory_map(&kparams.mmap))) goto cleanup;
    
    // Exit boot services before calling kernel
    UINTN retries = 0;
    const UINTN MAX_RETRIES = 5;
    while (EFI_ERROR(bs->ExitBootServices(image, kparams.mmap.key)) && retries < MAX_RETRIES) {
        // firmware could do a partial shutdown, need to get memory map again
        //   and try exit boot services again
        bs->FreePool(kparams.mmap.map);
        if (EFI_ERROR(get_memory_map(&kparams.mmap))) goto cleanup;
        retries++;
    }
    if (retries == MAX_RETRIES) {
        error(0, u"Could not Exit Boot Services!\r\n");
        goto cleanup;
    }
    
#endif
    
    
#if 1
    // Initialize page tables
    arch_init_page_tables(&kparams.mmap);
    
    // Identity mapping all available memory
    identity_map_efi_mmap(&kparams.mmap);
    
    // Identity map runtime services memory & set new runtime address map
    set_runtime_address_map(&kparams.mmap);
    
    // Remap kernel to higher address
    // including entry point (and kparams?)
    for (UINTN i = 0; i < (kernel_size + (PAGE_SIZE - 1)) / PAGE_SIZE; i++) {
        arch_map_page(kernel_buffer + (i*PAGE_SIZE),
                      KERNEL_START_ADDRESS + (i*PAGE_SIZE),
                      &kparams.mmap);
    }
    
    // NOTE: Remap kparams to higher address?
    // Identity map framebuffer
    for (UINTN i = 0; i < (kparams.gop_mode.FrameBufferSize + (PAGE_SIZE - 1)) / PAGE_SIZE; i++) {
        identity_map_page(kparams.gop_mode.FrameBufferBase + (i*PAGE_SIZE), &kparams.mmap);
    }
    
    
    
    // Identity map new stack for kernel
    const UINTN STACK_PAGES = 16;
    void *kernel_stack = mmap_allocate_pages(&kparams.mmap, STACK_PAGES);
    uint32_t stack_size = STACK_PAGES * PAGE_SIZE;
    memset(kernel_stack, 0, stack_size);
    
    for (UINTN i = 0; i < STACK_PAGES; i++) {
        identity_map_page((UINTN)kernel_stack + (i*PAGE_SIZE), &kparams.mmap);
    }
    
    
    TSS tss = {.io_map_base = sizeof(TSS)};
    UINTN tss_address = (UINTN)&tss;
    
    GDT gdt = {
        .null.value           = 0x0000000000000000, // Null descriptor
        
        .kernel_code_64.value = 0x00AF9A000000FFFF,
        .kernel_data_64.value = 0x00CF92000000FFFF,
        
        .user_code_64.value   = 0x00AFFA000000FFFF,
        .user_data_64.value   = 0x00CFF2000000FFFF,
        
        .kernel_code_32.value = 0x00CF9A000000FFFF,
        .kernel_data_32.value = 0x00CF92000000FFFF,
        
        .user_code_32.value   = 0x00CFFA000000FFFF,
        .user_data_32.value   = 0x00CFF2000000FFFF,
        
        .tss = {
            .descriptor = {
                .limit_15_0 = sizeof tss - 1,
                .base_15_0  = tss_address & 0xFFFF,
                .base_23_16 = (tss_address >> 16) & 0xFF,
                .type       = 9,    // 0b1001 64 bit TSS (available)
                .p          = 1,    // Present
                .base_31_24 = (tss_address >> 24) & 0xFF,
            },
                .base_63_32 = (tss_address >> 32) & 0xFFFFFFFF,
        }
    };
    
    Descriptor_Register gdtr = {.limit = sizeof gdt - 1, .base = (UINT64)&gdt};
    
    
    
    Kernel_Params *kparams_ptr = &kparams;
    
    
    
    // Clear interrupts before setting up new GDT/paging/etc.
    __asm__ __volatile__(
                         "cli\n"                     // Clear interrupts before setting new GDT/TSS, etc.
                         "movq %[pml4], %%CR3\n"     // Load new page tables
                         "lgdt %[gdt]\n"             // Load new GDT from gdtr register
                         "ltr %[tss]\n"              // Load new task register with new TSS value (byte offset into GDT)
                         
                         // Jump to new code segment in GDT (offset in GDT of 64 bit kernel/system code segment)
                         "pushq $0x8\n"
                         "leaq 1f(%%RIP), %%RAX\n"
                         "pushq %%RAX\n"
                         "lretq\n"
                         
                         // Executing code with new Code segment now, set up remaining segment registers
                         "1:\n"
                         "movq $0x10, %%RAX\n"   // Data segment to use (64 bit kernel data segment, offset in GDT)
                         "movq %%RAX, %%DS\n"    // Data segment
                         "movq %%RAX, %%ES\n"    // Extra segment
                         "movq %%RAX, %%FS\n"    // Extra segment (2), these also have different uses in Long Mode
                         "movq %%RAX, %%GS\n"    // Extra segment (3), these also have different uses in Long Mode
                         "movq %%RAX, %%SS\n"    // Stack segment
                         
                         // Set new stack value to use (for SP/stack pointer, etc.)
                         "movq %[stack], %%RSP\n"
                         
                         // Call new entry point in higher memory
                         "callq *%[entry]\n" // First parameter is kparams in RCX in input constraints below, for MS ABI
                         :
                         : [pml4]"r"(pml4), [gdt]"m"(gdtr), [tss]"r"((uint16_t)offsetof(GDT, tss)),
                         [stack]"gm"((uint64_t)kernel_stack + stack_size),    // Top of newly allocated stack
                         [entry]"r"(higher_entry_point), "c"(kparams_ptr)
                         : "rax", "memory");
    
    // TODO: Set new page tables (CR3 = PML4) and GDT (lgdt && ltr), and call entry point with params
    
    // TODO: Test calling PE, ELF, and flat bin kernels/entry points
    
#endif
    
    // Call the kernel/OS here, Fully in control now, not in EFI anymore!
    // higher_entry_point(&kparams);
    
    __builtin_unreachable();
    
    // TODO: Check file format (ELF, PE, etc.) and load accordingly
    
    // Final cleanup
cleanup:
    bs->FreePool(file_buffer);
    bs->FreePool(disk_buffer); // Free memory allocated for data partition file
    
exit:
    printf_c16(u"Press any key to go back...\r\n");
    EFI_INPUT_KEY key = get_key();
    printf_c16(u"key.ScanCode:%u",key.ScanCode);
    return EFI_SUCCESS;
}

// ===========================================================
// Timer function to print current date/time every 1 second
// ===========================================================
VOID EFIAPI print_datetime(__attribute__((unused)) IN EFI_EVENT event, IN VOID *Context) {
    Timer_Context context = *(Timer_Context *)Context;
    
    // Save current cursor position before printing date/time
    UINT32 save_col = cout->Mode->CursorColumn, save_row = cout->Mode->CursorRow;
    
    // Get current date/time
    EFI_TIME time;
    EFI_TIME_CAPABILITIES capabilities;
    rs->GetTime(&time, &capabilities);
    
    // Move cursor to print in lower right corner
    cout->SetCursorPosition(cout, context.cols-20, context.rows-1);
    
    // Print current date/time
    printf_c16(u"%u-%c%u-%c%u %c%u:%c%u:%c%u",
               time.Year,
               time.Month  < 10 ? u'0' : u'\0', time.Month,
               time.Day    < 10 ? u'0' : u'\0', time.Day,
               time.Hour   < 10 ? u'0' : u'\0', time.Hour,
               time.Minute < 10 ? u'0' : u'\0', time.Minute,
               time.Second < 10 ? u'0' : u'\0', time.Second);
    
    // Restore cursor position
    cout->SetCursorPosition(cout, save_col, save_row);
}





typedef struct {
    UINT8  signature[4];
    UINT32 length;
    UINT8  revision;
    UINT8  checksum;
    UINT8  OEMID[6];
    UINT8  OEM_table_id[8];
    UINT32 OEM_revision;
    UINT32 creator_id;
    UINT32 creator_revision;
} __attribute__((packed)) ACPI_Description_Header;

EFI_STATUS print_config_tables(void){
    cout->ClearScreen(cout);
    
    bs->CloseEvent(timer_event);
    
    printf_c16(u"Configuration Table GUIDs:\r\n");
    
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        print_guid(st->ConfigurationTable[i].VendorGuid);
        printf_c16(u"\r\n");
    }
    
    printf_c16(u"\r\nPress any key to go back...\r\n");
    
    get_key();
    
    return EFI_SUCCESS;
}

EFI_STATUS print_acpi_tables(void) {
    cout->ClearScreen(cout);
    
    bs->CloseEvent(timer_event);
    
    EFI_GUID acpi_guid = EFI_ACPI_TABLE_GUID;
    VOID *rsdp_ptr = get_config_table_by_guid(acpi_guid);
    bool acpi_20 = false;
    if (!rsdp_ptr){
        
        acpi_guid = (EFI_GUID)ACPI_TABLE_GUID;
        VOID *rsdp_ptr = get_config_table_by_guid(acpi_guid);
        
        if (!rsdp_ptr){
            // Check for ACPI 1.0 table as fallback
            printf_c16(u"Error: Could not find ACPI configuration table\r\n");
            return 1;
        }else{
            printf_c16(u"ACPI 1.0 Table found at %x\r\n",rsdp_ptr);
        }
    }else{
        printf_c16(u"ACPI 2.0 Table found at %x\r\n",rsdp_ptr);
        acpi_20 = true;
    }
    
    
    
    UINT8 *rsdp = rsdp_ptr;
    if(acpi_20){
        printf_c16(u"RSDP:\r\n"
                   u"Signature: %c%c%c%c%c%c%c%c\r\n"
                   u"Checksum: %u\r\n"
                   u"OEMID: %c%c%c%c%c%c\r\n"
                   u"RSDT Address: %x\r\n"
                   u"Length: %u\r\n"
                   u"XSDT Address: %x\r\n"
                   u"Extended Checksum: %u\r\n",
                   rsdp[0],rsdp[1],rsdp[2],rsdp[3],rsdp[4],rsdp[5],rsdp[6],rsdp[7],
                   (UINTN)rsdp[8],
                   rsdp[9],rsdp[10],rsdp[11],rsdp[12],rsdp[13],rsdp[14],
                   *(UINT32 *)&rsdp[16],
                   *(UINT32 *)&rsdp[20],
                   *(UINT32 *)&rsdp[24],
                   *(UINT32 *)&rsdp[32]
                   );
    }else{
        printf_c16(u"RSDP:\r\n"
                   u"Signature: %c%c%c%c%c%c%c%c\r\n"
                   u"Checksum: %u\r\n"
                   u"OEMID: %c%c%c%c%c%c\r\n"
                   u"RSDT Address: %x\r\n",
                   rsdp[0],rsdp[1],rsdp[2],rsdp[3],rsdp[4],rsdp[5],rsdp[6],rsdp[7],
                   (UINTN)rsdp[8],
                   rsdp[9],rsdp[10],rsdp[11],rsdp[12],rsdp[13],rsdp[14],
                   *(UINT32 *)&rsdp[16]
                   );
    }
    
    printf_c16(u"\r\nPress any key to print RSDT/XSDT...\r\n");
    get_key();
    
    ACPI_Description_Header *header = NULL;
    UINT64 xsdt_address = *(UINT64 *)&rsdp[24];
    if (acpi_20) {
        // Print XSDT header
        header = (ACPI_Description_Header *)(UINTN)xsdt_address;
        printf_c16(u"\r\nXSDT @ %x\r\n", (UINTN)header);
        print_acpi_table_header(*(ACPI_TABLE_HEADER *)header);
        
        // Print XSDT entries (each entry is a 64-bit physical address)
        printf_c16(u"\r\nPress any key to print entries...\r\n");
        get_key();
        
        printf_c16(u"Entries:\r\n");
        UINT64 *entry = (UINT64 *)((UINT8 *)header + sizeof(*header));
        for (UINTN i = 0; i < (header->length - sizeof(*header)) / 8; i++) {
            ACPI_Description_Header *table_header = (ACPI_Description_Header *)(UINTN)entry[i];
            printf_c16(u"%c%c%c%c\r\n",
                       table_header->signature[0], table_header->signature[1],
                       table_header->signature[2], table_header->signature[3]);
            // TODO: Print more than only the signature
        }
    } else {
        // TODO: Print RSDT header & entries (32-bit pointers)
        printf_c16(u"\r\nACPI 1.0 detected; RSDT parsing not implemented yet.\r\n");
    }
    
    
    printf_c16(u"\r\nPress any key to go back...\r\n");
    get_key();
    return EFI_SUCCESS;
}


EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable){
    
    init_global_varibles(ImageHandle,SystemTable);
    
    cout->Reset(cout, false);
    
    cout->SetAttribute(cout, EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLUE));
    
    bool running = true;
    
    while (running) {
        const CHAR16 *menu_choices[] = {
            u"Set Text Mode",
            u"Set Graphics Mode",
            u"Test Mouse",
            u"Read ESP Files",
            u"Print Block IO Paritions",
            u"Read Data Partition File",
            u"Load Kernel",
            u"Print Memory Map",
            u"Print Config Tables",
            u"Print ACPI Tables"
        };
        
        EFI_STATUS (*menu_funcs[])(void) = {
            set_text_mode,
            set_graphics_mode,
            test_mouse,
            read_esp_files,
            print_block_io_partitions,
            read_data_partition_file,
            load_kernel,
            print_memory_map,
            print_config_tables,
            print_acpi_tables
        };
        cout->ClearScreen(cout);
        
        UINTN cols = 0, rows = 0;
        cout->QueryMode(cout, cout->Mode->Mode, &cols, &rows);
        
        // Timer function context will be the text mode screen bounds
        typedef struct {
            UINT32 rows;
            UINT32 cols;
        } Timer_Context;
        
        Timer_Context context = { .rows = rows, .cols = cols };
        
        // Close Timer Event for cleanup
        bs->CloseEvent(timer_event);
        
        // Create timer event, to print date/time on screen every ~1second
        bs->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL,
                        TPL_CALLBACK,
                        print_datetime,
                        (VOID *)&context,
                        &timer_event);
        
        // Set Timer for the timer event to run every 1 second (in 100ns units)
        bs->SetTimer(timer_event, TimerPeriodic, 10000000);
        
        // Print keybinds at bottom of screen
        cout->SetCursorPosition(cout, 0, rows-3);
        printf_c16(u"Up/Down Arrow = Move cursor\r\n"
                   u"Enter = Select\r\n"
                   u"Escape = Shutdown");
        
        // Print menu choices
        // Highlight first choice as initial choice
        cout->SetCursorPosition(cout, 0, 0);
        cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
        printf_c16(u"%s", menu_choices[0]);
        
        // Print rest of choices
        cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
        for (UINTN i = 1; i < ARRAY_SIZE(menu_choices); i++)
            printf_c16(u"\r\n%s", menu_choices[i]);
        
        // Get cursor row boundaries
        INTN max_row = cout->Mode->CursorRow;
        
        // Input loop
        cout->SetCursorPosition(cout, 0, 0);
        
        bool getting_input = true;
        while (getting_input) {
            INTN current_row = cout->Mode->CursorRow;
            EFI_INPUT_KEY key = get_key();
            
            // Process input
            switch (key.ScanCode) {
                case SCANCODE_UP_ARROW:
                case SCANCODE_DOWN_ARROW:
                    // De-highlight current row
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                    printf_c16(u"%s\r", menu_choices[current_row]);
                    
                    // Go up or down 1 row in range [0:max_row] (circular buffer)
                    current_row = (key.ScanCode == SCANCODE_UP_ARROW)
                    ? (current_row + max_row) % (max_row+1)
                    : (current_row+1) % (max_row+1);
                    
                    // Highlight new current row
                    cout->SetCursorPosition(cout, 0, current_row);
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(HIGHLIGHT_FG_COLOR, HIGHLIGHT_BG_COLOR));
                    printf_c16(u"%s\r", menu_choices[current_row]);
                    
                    // Reset colors
                    cout->SetAttribute(cout, EFI_TEXT_ATTR(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
                    break;
                    
                case SCANCODE_ESC:
                    // Escape key: power off
                    rs->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
                    
                    // !NOTE!: This should not return, system should power off
                    __builtin_unreachable();
                    break;
                    
                default:
                    if (key.UnicodeChar == u'\r') {
                        cout->ClearScreen(cout);
                        
                        // Enter key, select choice
                        EFI_STATUS return_status = menu_funcs[current_row]();
                        if (EFI_ERROR(return_status))
                            printf_c16(u"Press any key to go back...");
                        
                        // Will leave input loop and reprint main menu
                        getting_input = false;
                    }
                    break;
            }
        }
    }
    
    return EFI_SUCCESS;
}
