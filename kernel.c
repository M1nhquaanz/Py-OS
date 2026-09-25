#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "py/mpconfig.h"
#include "py/obj.h"
#include "mphalport.h"

void mp_hal_stdout_tx_strn(const char *str, size_t len);

#include "py/runtime.h"
#include "py/gc.h"
#include "py/compile.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "py/parse.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

#define COLOR_BLACK         0
#define COLOR_BLUE          1
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_RED           4
#define COLOR_MAGENTA       5
#define COLOR_BROWN         6
#define COLOR_LIGHT_GRAY    7
#define COLOR_DARK_GRAY     8
#define COLOR_LIGHT_BLUE    9
#define COLOR_LIGHT_GREEN   10
#define COLOR_LIGHT_CYAN    11
#define COLOR_LIGHT_RED     12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW        14
#define COLOR_WHITE         15

#define VGA_COLOR(fg, bg) (((bg) << 4) | (fg))

#define KEY_ENTER       0x0A
#define KEY_SHIFT_ENTER 0x84
#define KEY_BACKSPACE   0x08
#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83

#define PROMPT_LEN 13
#define HIST_BUF_SIZE 4096
#define MP_HEAP_SIZE (256 * 1024)
#define MAX_FILES 32
#define MAX_FILE_NAME 32
#define MAX_FILE_SIZE 2048

void kernel_main(void) __asm__("kernel_main");

typedef struct {
    char name[MAX_FILE_NAME];
    char content[MAX_FILE_SIZE];
    size_t size;
    bool used;
} vfs_file_t;

static vfs_file_t vfs_disk[MAX_FILES];

static char active_filename[MAX_FILE_NAME] = {0};
static char active_buffer[MAX_FILE_SIZE]   = {0};
static size_t active_buf_len               = 0;
static bool is_file_open                   = false;

static volatile uint16_t* const vga_buffer = (volatile uint16_t*)VGA_ADDRESS;
static size_t terminal_row = 4;
static size_t terminal_column = 1;

static uint8_t terminal_color = VGA_COLOR(COLOR_WHITE,       COLOR_BLACK);
static uint8_t result_color   = VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK);
static uint8_t input_color    = VGA_COLOR(COLOR_WHITE,       COLOR_BLACK);
static uint8_t err_color      = VGA_COLOR(COLOR_LIGHT_RED,   COLOR_BLACK);

static bool shift_pressed = false;
static bool is_extended = false;
static uint32_t timer_ticks = 0;
static char mp_heap[MP_HEAP_SIZE];

static const char scancode_ascii[] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',  '\b',
    '\t','q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,    ' ', 0
};

static const char scancode_ascii_shift[] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',  '\b',
    '\t','Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"','~',
    0,   '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,    ' ', 0
};

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest; const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = (char *)dest; const char *s = (const char *)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) { if (!*s++) return NULL; }
    return (char *)s;
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_set_cursor(size_t row, size_t col) {
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_enable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 13);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
}

static void redraw_borders(void) {
    uint8_t frame_color = VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLUE);
    for (int y = 4; y < VGA_HEIGHT - 1; y++) {
        vga_buffer[y * VGA_WIDTH] = (uint16_t)'\xBA' | ((uint16_t)frame_color << 8);
        vga_buffer[y * VGA_WIDTH + 79] = (uint16_t)'\xBA' | ((uint16_t)frame_color << 8);
    }
}

static void scroll_up(void) {
    for (size_t y = 5; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 1; x < VGA_WIDTH - 1; x++) {
            vga_buffer[(y - 1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
        }
    }
    for (size_t x = 1; x < VGA_WIDTH - 1; x++) {
        vga_buffer[(VGA_HEIGHT - 2) * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)terminal_color << 8);
    }
    terminal_row = VGA_HEIGHT - 2;
    redraw_borders();
}

void kputc_color(char c, uint8_t color) {
    if (c == '\r') {
        terminal_column = 1;
        return;
    }

    if (c == '\n') {
        terminal_column = 1;
        if (++terminal_row == VGA_HEIGHT - 1) scroll_up();
        return;
    }

    if (c == '\b') {
        if (terminal_column > 1) {
            terminal_column--;
            vga_buffer[terminal_row * VGA_WIDTH + terminal_column] = (uint16_t)' ' | ((uint16_t)color << 8);
        }
        return;
    }

    if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
        vga_buffer[terminal_row * VGA_WIDTH + terminal_column] = (uint16_t)(unsigned char)c | ((uint16_t)color << 8);
        if (++terminal_column == VGA_WIDTH - 1) {
            terminal_column = 1;
            if (++terminal_row == VGA_HEIGHT - 1) scroll_up();
        }
    }
}

void kputc(char c) { kputc_color(c, terminal_color); }

static void print_prompt(void) {
    uint8_t root_color   = VGA_COLOR(COLOR_LIGHT_RED, COLOR_BLACK);
    uint8_t path_color   = VGA_COLOR(COLOR_LIGHT_BLUE, COLOR_BLACK);
    uint8_t file_color   = VGA_COLOR(COLOR_YELLOW, COLOR_BLACK);
    uint8_t symbol_color = VGA_COLOR(COLOR_WHITE, COLOR_BLACK);

    for (const char *p = "root@pyos"; *p; p++) kputc_color(*p, root_color);
    kputc_color(':', symbol_color);
    for (const char *p = "~"; *p; p++) kputc_color(*p, path_color);

    if (is_file_open) {
        kputc_color('[', symbol_color);
        for (const char *p = active_filename; *p; p++) kputc_color(*p, file_color);
        kputc_color(']', symbol_color);
    }

    for (const char *p = "# "; *p; p++) kputc_color(*p, symbol_color);
    vga_set_cursor(terminal_row, terminal_column);
}

static void draw_desktop_layout(void) {
    uint8_t frame_color  = VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLUE);
    uint8_t topbar_bg    = VGA_COLOR(COLOR_BLACK, COLOR_LIGHT_GRAY);
    uint8_t status_bg    = VGA_COLOR(COLOR_WHITE, COLOR_BLUE);
    uint8_t body_bg      = VGA_COLOR(COLOR_WHITE, COLOR_BLACK);

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)body_bg << 8);
        }
    }

    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[x] = (uint16_t)' ' | ((uint16_t)topbar_bg << 8);
    }
    const char* sys_title = " [PyOS v1.0 Enhanced VGA]";
    for (size_t i = 0; i < strlen(sys_title); i++) {
        vga_buffer[i + 1] = (uint16_t)sys_title[i] | ((uint16_t)VGA_COLOR(COLOR_RED, COLOR_LIGHT_GRAY) << 8);
    }

    const char* sys_stats = "Arch: x86_32 | RAM Heap: 256KB | Status: OK";
    for (size_t i = 0; i < strlen(sys_stats); i++) {
        vga_buffer[VGA_WIDTH - strlen(sys_stats) - 2 + i] = (uint16_t)sys_stats[i] | ((uint16_t)topbar_bg << 8);
    }

    vga_buffer[1 * VGA_WIDTH] = (uint16_t)'\xC9' | ((uint16_t)frame_color << 8);
    for (int i = 1; i < 79; i++) vga_buffer[1 * VGA_WIDTH + i] = (uint16_t)'\xCD' | ((uint16_t)frame_color << 8);
    vga_buffer[1 * VGA_WIDTH + 79] = (uint16_t)'\xBB' | ((uint16_t)frame_color << 8);

    redraw_borders();

    vga_buffer[3 * VGA_WIDTH] = (uint16_t)'\xCC' | ((uint16_t)frame_color << 8);
    for (int i = 1; i < 79; i++) vga_buffer[3 * VGA_WIDTH + i] = (uint16_t)'\xC4' | ((uint16_t)frame_color << 8);
    vga_buffer[3 * VGA_WIDTH + 79] = (uint16_t)'\xB9' | ((uint16_t)frame_color << 8);

    const char* win_title = " Terminal - MicroPython Interpreter Subsystem ";
    for (size_t i = 0; i < strlen(win_title); i++) {
        vga_buffer[2 * VGA_WIDTH + 2 + i] = (uint16_t)win_title[i] | ((uint16_t)VGA_COLOR(COLOR_YELLOW, COLOR_BLACK) << 8);
    }

    for (int i = 0; i < 80; i++) vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = (uint16_t)' ' | ((uint16_t)status_bg << 8);
    const char* status_text = " [F1] Clear | Commands: help, ls, new, open, save, close, cat, rm, run ";
    for (size_t i = 0; i < strlen(status_text); i++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + 1 + i] = (uint16_t)status_text[i] | ((uint16_t)status_bg << 8);
    }

    terminal_row = 4;
    terminal_column = 1;
}

void clear_screen(void) {
    draw_desktop_layout();
    print_prompt();
}

static void sleep_ms(uint32_t ms) {
    while (ms--) {
        outb(0x43, 0xB0);
        outb(0x42, 1193 & 0xFF);
        outb(0x42, (1193 >> 8) & 0xFF);

        uint8_t control = inb(0x61);
        outb(0x61, (control & 0xDC) | 0x01);
        
        while (!(inb(0x61) & 0x20));
    }
}

static void play_boot_intro(void) {
    uint8_t bg_black = VGA_COLOR(COLOR_WHITE, COLOR_BLACK);
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | ((uint16_t)bg_black << 8);
    }

    const char *logo[] = {
        "  ____  _   _  ___  ____  ",
        " |  _ \\| | | |/ _ \\/ ___| ",
        " | |_) | |_| | | | \\___ \\ ",
        " |  __/ \\__, | |_| |___) |",
        " |_|    |___/ \\___/|____/ "
    };

    uint8_t logo_color  = VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK);
    uint8_t text_color  = VGA_COLOR(COLOR_WHITE, COLOR_BLACK);
    uint8_t ok_color    = VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK);

    size_t start_row = 4;
    for (int r = 0; r < 5; r++) {
        size_t start_col = (VGA_WIDTH - strlen(logo[r])) / 2;
        for (size_t c = 0; c < strlen(logo[r]); c++) {
            vga_buffer[(start_row + r) * VGA_WIDTH + (start_col + c)] = 
                (uint16_t)logo[r][c] | ((uint16_t)logo_color << 8);
        }
        sleep_ms(150);
    }

    const char *sub = "MicroPython Baremetal Operating System";
    size_t sub_col = (VGA_WIDTH - strlen(sub)) / 2;
    for (size_t i = 0; i < strlen(sub); i++) {
        vga_buffer[(start_row + 6) * VGA_WIDTH + (sub_col + i)] = 
            (uint16_t)sub[i] | ((uint16_t)VGA_COLOR(COLOR_YELLOW, COLOR_BLACK) << 8);
        sleep_ms(30);
    }

    sleep_ms(500);

    const char *boot_logs[] = {
        "Initializing x86_32 GDT & IDT Architecture...",
        "Allocating 256KB Dynamic GC Heap Space...",
        "Mounting Virtual RAM Disk File System (VFS)...",
        "Loading MicroPython Core Interpreter v1.20...",
        "Starting Desktop Terminal Shell Interface...",
        "System Ready!"
    };

    size_t log_row = 13;
    for (int i = 0; i < 6; i++) {
        size_t col = 12;
        
        for (const char *p = boot_logs[i]; *p; p++) {
            vga_buffer[log_row * VGA_WIDTH + col++] = (uint16_t)*p | ((uint16_t)text_color << 8);
            sleep_ms(20); 
        }

        sleep_ms(300);
        
        const char *ok_str = " [ OK ]";
        for (const char *p = ok_str; *p; p++) {
            vga_buffer[log_row * VGA_WIDTH + col++] = (uint16_t)*p | ((uint16_t)ok_color << 8);
        }

        sleep_ms(600);
        log_row++;
    }

    sleep_ms(1500);
}

void mp_hal_stdout_tx_strn(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\n') {
            kputc_color('\n', result_color);
        } else if (c == '\r') {
            continue;
        } else if (c == '\t') {
            for (int t = 0; t < 4; t++) kputc_color(' ', result_color);
        } else {
            kputc_color(c, result_color);
        }
    }
    vga_set_cursor(terminal_row, terminal_column);
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    mp_hal_stdout_tx_strn(str, len);
}

int mp_hal_stdout_tx_str(const char *str) {
    if (!str) return 0;
    size_t len = strlen(str);
    mp_hal_stdout_tx_strn(str, len);
    return (int)len;
}

void mp_plat_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    mp_hal_stdout_tx_strn(str, len);
}

uint32_t mp_hal_ticks_ms(void) { return timer_ticks++; }
uint32_t mp_hal_ticks_us(void) { return timer_ticks * 1000; }
void     mp_hal_delay_ms(uint32_t ms) { sleep_ms(ms); }
void     mp_hal_delay_us(uint32_t us) { for (volatile uint32_t i = 0; i < us * 10; i++); }

static int mp_interrupt_char = -1;
void mp_hal_set_interrupt_char(int c) { mp_interrupt_char = c; }
int  mp_hal_get_interrupt_char(void) { return mp_interrupt_char; }

void nlr_jump_fail(void *val) {
    (void)val;
    for (const char *p = "\n[FATAL] MicroPython Execution Fault!\n"; *p; p++) {
        kputc_color(*p, err_color);
    }
}

int mp_import_stat(const char *path) { (void)path; return 0; }
mp_lexer_t *mp_lexer_new_from_file(const char *filename) { (void)filename; return NULL; }

void gc_collect(void) {
    gc_collect_start();
    void *sp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(sp));
    gc_collect_root((void**)sp, ((uint32_t)MP_STATE_THREAD(stack_top) - (uint32_t)sp) / sizeof(uint32_t));
    gc_collect_end();
}

void mp_handle_pending(bool status) { (void)status; }

uint8_t get_key_event(void) {
    while (1) {
        if (inb(0x64) & 0x01) {
            uint8_t sc = inb(0x60);
            if (sc == 0xE0) { is_extended = true; continue; }
            if (sc == 0x2A || sc == 0x36) { shift_pressed = true; continue; }
            if (sc == 0xAA || sc == 0xB6) { shift_pressed = false; continue; }

            if (sc & 0x80) { is_extended = false; continue; }

            if (is_extended) {
                is_extended = false;
                if (sc == 0x48) return KEY_UP;
                if (sc == 0x50) return KEY_DOWN;
                if (sc == 0x4B) return KEY_LEFT;
                if (sc == 0x4D) return KEY_RIGHT;
            } else {
                if (sc == 0x3B) { clear_screen(); continue; }
                if (sc == 0x1C) return shift_pressed ? KEY_SHIFT_ENTER : KEY_ENTER;
                if (sc == 0x0E) return KEY_BACKSPACE;

                if (sc < sizeof(scancode_ascii)) {
                    char k = shift_pressed ? scancode_ascii_shift[sc] : scancode_ascii[sc];
                    if (k) {
                        for (volatile int i = 0; i < 50000; i++);
                        return (uint8_t)k;
                    }
                }
            }
        }
    }
}

int mp_hal_stdin_rx_chr(void) {
    while (1) {
        uint8_t key = get_key_event();
        if (key == KEY_ENTER) {
            kputc('\n');
            return '\n';
        }
        if (key == KEY_BACKSPACE) {
            kputc('\b');
            return '\b';
        }
        if (key >= 32 && key <= 126) {
            kputc_color((char)key, input_color);
            return (int)key;
        }
    }
}

static void init_vfs(void) {
    for (int i = 0; i < MAX_FILES; i++) vfs_disk[i].used = false;

    vfs_disk[0].used = true;
    memcpy(vfs_disk[0].name, "main.py", 7);
    const char* demo = "print('Hello from PyOS MicroPython Engine!')\nfor i in range(3):\n    print('--> Iteration:', i)";
    memcpy(vfs_disk[0].content, demo, strlen(demo));
    vfs_disk[0].size = strlen(demo);
}

static vfs_file_t* vfs_find(const char *filename) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (vfs_disk[i].used && strcmp(vfs_disk[i].name, filename) == 0) {
            return &vfs_disk[i];
        }
    }
    return NULL;
}

static void shell_touch(const char *filename) {
    if (vfs_find(filename)) {
        mp_hal_stdout_tx_str("\ntouch: File already exists.\n\n");
        return;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (!vfs_disk[i].used) {
            vfs_disk[i].used = true;
            memcpy(vfs_disk[i].name, filename, strlen(filename));
            vfs_disk[i].name[strlen(filename)] = '\0';
            vfs_disk[i].content[0] = '\0';
            vfs_disk[i].size = 0;
            mp_hal_stdout_tx_str("\nFile created successfully.\n\n");
            return;
        }
    }
    mp_hal_stdout_tx_str("\ntouch: RAM disk full.\n\n");
}

static void shell_write(const char *args) {
    char name[MAX_FILE_NAME];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < MAX_FILE_NAME - 1) {
        name[i] = args[i];
        i++;
    }
    name[i] = '\0';

    if (args[i] != ' ') {
        mp_hal_stdout_tx_str("\nUsage: write <filename> <content>\n\n");
        return;
    }

    const char *content = args + i + 1;
    vfs_file_t *file = vfs_find(name);
    if (!file) {
        mp_hal_stdout_tx_str("\nwrite: File not found.\n\n");
        return;
    }

    size_t len = strlen(content);
    if (len >= MAX_FILE_SIZE) len = MAX_FILE_SIZE - 1;
    memcpy(file->content, content, len);
    file->content[len] = '\0';
    file->size = len;
    mp_hal_stdout_tx_str("\nFile written successfully.\n\n");
}

static void shell_rm(const char *filename) {
    vfs_file_t *file = vfs_find(filename);
    if (file) {
        file->used = false;
        mp_hal_stdout_tx_str("\nFile deleted.\n\n");
    } else {
        mp_hal_stdout_tx_str("\nrm: File not found.\n\n");
    }
}

static void shell_ls(void) {
    kputc_color('\n', VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    mp_hal_stdout_tx_str("FILENAME            SIZE (Bytes)   TYPE\n");
    mp_hal_stdout_tx_str("---------------------------------------\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (vfs_disk[i].used) {
            mp_hal_stdout_tx_str(vfs_disk[i].name);
            for (size_t s = strlen(vfs_disk[i].name); s < 20; s++) kputc(' ');

            char buf[16];
            int sz = vfs_disk[i].size;
            int idx = 0;
            if (sz == 0) buf[idx++] = '0';
            else {
                char tmp[16]; int t = 0;
                while (sz > 0) { tmp[t++] = '0' + (sz % 10); sz /= 10; }
                while (t > 0) buf[idx++] = tmp[--t];
            }
            buf[idx] = '\0';
            mp_hal_stdout_tx_str(buf);
            for (size_t s = strlen(buf); s < 15; s++) kputc(' ');

            mp_hal_stdout_tx_str("RAM File\n");
        }
    }
    kputc('\n');
}

static void shell_cat(const char *filename) {
    vfs_file_t *file = vfs_find(filename);
    if (file) {
        kputc('\n');
        mp_hal_stdout_tx_str(file->content);
        kputc('\n');
    } else {
        mp_hal_stdout_tx_str("\ncat: File not found.\n\n");
    }
}

static void shell_new(const char *filename) {
    if (strlen(filename) == 0) {
        mp_hal_stdout_tx_str("\nUsage: new <filename>\n\n");
        return;
    }

    vfs_file_t *file = vfs_find(filename);
    if (!file) {
        shell_touch(filename);
        file = vfs_find(filename);
    }

    if (file) {
        memcpy(active_filename, filename, strlen(filename) + 1);
        memset(active_buffer, 0, MAX_FILE_SIZE);
        active_buf_len = 0;
        is_file_open = true;

        kputc_color('\n', VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
        mp_hal_stdout_tx_str("[Editor] Opened buffer: ");
        mp_hal_stdout_tx_str(active_filename);
        mp_hal_stdout_tx_str("\n[Editor] Write code into shell, then type 'save' to commit.\n\n");
    }
}

static void shell_open(const char *filename) {
    if (strlen(filename) == 0) {
        mp_hal_stdout_tx_str("\nUsage: open <filename>\n\n");
        return;
    }

    vfs_file_t *file = vfs_find(filename);
    if (!file) {
        mp_hal_stdout_tx_str("\nopen: File not found.\n\n");
        return;
    }

    memcpy(active_filename, file->name, strlen(file->name) + 1);
    memcpy(active_buffer, file->content, file->size + 1);
    active_buf_len = file->size;
    is_file_open = true;

    kputc_color('\n', VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    mp_hal_stdout_tx_str("=== Opened: ");
    mp_hal_stdout_tx_str(active_filename);
    mp_hal_stdout_tx_str(" ===\n");
    mp_hal_stdout_tx_str(active_buffer);
    mp_hal_stdout_tx_str("\n======================\n\n");
}

static void shell_save(const char *target_name) {
    if (!is_file_open && strlen(target_name) == 0) {
        mp_hal_stdout_tx_str("\nsave: No active file buffer open.\n\n");
        return;
    }

    const char *save_as = (strlen(target_name) > 0) ? target_name : active_filename;
    vfs_file_t *file = vfs_find(save_as);

    if (!file) {
        shell_touch(save_as);
        file = vfs_find(save_as);
    }

    if (file) {
        memcpy(file->content, active_buffer, active_buf_len);
        file->content[active_buf_len] = '\0';
        file->size = active_buf_len;

        memcpy(active_filename, save_as, strlen(save_as) + 1);
        is_file_open = true;

        kputc_color('\n', VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
        mp_hal_stdout_tx_str("[VFS] Saved successfully.\n\n");
    }
}

static void shell_close(void) {
    if (!is_file_open) {
        mp_hal_stdout_tx_str("\nclose: No active file buffer.\n\n");
        return;
    }
    is_file_open = false;
    active_filename[0] = '\0';
    active_buffer[0] = '\0';
    active_buf_len = 0;
    mp_hal_stdout_tx_str("\nClosed current file buffer.\n\n");
}

static void shell_fetch(void) {
    uint8_t c_logo  = VGA_COLOR(COLOR_LIGHT_RED,     COLOR_BLACK);
    uint8_t c_title = VGA_COLOR(COLOR_YELLOW,        COLOR_BLACK);
    uint8_t c_label = VGA_COLOR(COLOR_LIGHT_CYAN,    COLOR_BLACK);
    uint8_t c_val   = VGA_COLOR(COLOR_WHITE,         COLOR_BLACK);
    uint8_t c_sep   = VGA_COLOR(COLOR_DARK_GRAY,     COLOR_BLACK);

    kputc('\n');
    for (const char *p = "  ____  _   _  ___  ____  "; *p; p++) kputc_color(*p, c_logo);
    for (const char *p = "PyOS Baremetal Kernel v1.0\n"; *p; p++) kputc_color(*p, c_title);

    for (const char *p = " |  _ \\| | | |/ _ \\/ ___| "; *p; p++) kputc_color(*p, c_logo);
    for (const char *p = "------------------------------------\n"; *p; p++) kputc_color(*p, c_sep);

    for (const char *p = " | |_) | |_| | | | \\___ \\ "; *p; p++) kputc_color(*p, c_logo);
    for (const char *p = "OS Arch   : "; *p; p++) kputc_color(*p, c_label);
    for (const char *p = "x86_32 Protected Mode\n"; *p; p++) kputc_color(*p, c_val);

    for (const char *p = " |  __/ \\__, | |_| |___) |"; *p; p++) kputc_color(*p, c_logo);
    for (const char *p = "Language  : "; *p; p++) kputc_color(*p, c_label);
    for (const char *p = "MicroPython Core v1.20\n"; *p; p++) kputc_color(*p, c_val);

    for (const char *p = " |_|    |___/ \\___/|____/ "; *p; p++) kputc_color(*p, c_logo);
    for (const char *p = "Memory    : "; *p; p++) kputc_color(*p, c_label);
    for (const char *p = "256KB Dynamic GC Heap\n\n"; *p; p++) kputc_color(*p, c_val);
}

static void shell_help(void) {
    kputc_color('\n', VGA_COLOR(COLOR_YELLOW, COLOR_BLACK));
    mp_hal_stdout_tx_str("PyOS Builtin Shell Commands:\n");
    mp_hal_stdout_tx_str("  help               - Show command list\n");
    mp_hal_stdout_tx_str("  fetch              - Display architecture system info\n");
    mp_hal_stdout_tx_str("  ls                 - List files in virtual RAM disk\n");
    mp_hal_stdout_tx_str("  new <file>         - Create and open a new file buffer\n");
    mp_hal_stdout_tx_str("  open <file>        - Open file contents into active buffer\n");
    mp_hal_stdout_tx_str("  save [file]        - Save current buffer to file\n");
    mp_hal_stdout_tx_str("  close              - Close active file buffer\n");
    mp_hal_stdout_tx_str("  touch <file>       - Create an empty file\n");
    mp_hal_stdout_tx_str("  write <file> <str> - Quick write string to file\n");
    mp_hal_stdout_tx_str("  cat <file>         - Display raw contents of a file\n");
    mp_hal_stdout_tx_str("  rm <file>          - Remove file from RAM disk\n");
    mp_hal_stdout_tx_str("  run <file>         - Execute Python script from VFS\n");
    mp_hal_stdout_tx_str("  clear              - Clear screen\n\n");
}

void execute_python_src(const char *src) {
    if (!src || strlen(src) == 0) return;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        if (lex) {
            qstr source_name = lex->source_name;
            mp_parse_input_kind_t parse_kind = strchr(src, '\n') ? MP_PARSE_FILE_INPUT : MP_PARSE_SINGLE_INPUT;

            mp_parse_tree_t parse_tree = mp_parse(lex, parse_kind);
            mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);

            kputc('\n');
            mp_call_function_0(module_fun);
            kputc('\n');
        }
        nlr_pop();
    } else {
        kputc('\n');
        for (const char *p = "[Python Exception] "; *p; p++) kputc_color(*p, err_color);
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        kputc('\n');
    }

    gc_collect();
}

static void shell_run(const char *filename) {
    vfs_file_t *file = vfs_find(filename);
    if (file) {
        execute_python_src(file->content);
    } else {
        for (const char *p = "\nrun: Script file not found.\n\n"; *p; p++) kputc_color(*p, err_color);
    }
}

static bool is_valid_python_syntax(const char *cmd) {
    if (strncmp(cmd, "print(", 6) == 0 ||
        strncmp(cmd, "for ", 4) == 0 ||
        strncmp(cmd, "while ", 6) == 0 ||
        strncmp(cmd, "if ", 3) == 0 ||
        strncmp(cmd, "def ", 4) == 0 ||
        strncmp(cmd, "import ", 7) == 0 ||
        strchr(cmd, '=') != NULL) {
        return true;
    }
    return false;
}

static void handle_command(const char *cmd) {
    if (strcmp(cmd, "help") == 0) {
        shell_help();
    } else if (strcmp(cmd, "fetch") == 0) {
        shell_fetch();
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
        return;
    } else if (strcmp(cmd, "uname") == 0) {
        mp_hal_stdout_tx_str("\nPyOS Baremetal Subsystem 1.0 (x86_32 Target)\n\n");
    } else if (strcmp(cmd, "ls") == 0) {
        shell_ls();
    } else if (strncmp(cmd, "new ", 4) == 0) {
        shell_new(cmd + 4);
    } else if (strncmp(cmd, "open ", 5) == 0) {
        shell_open(cmd + 5);
    } else if (strcmp(cmd, "save") == 0) {
        shell_save("");
    } else if (strncmp(cmd, "save ", 5) == 0) {
        shell_save(cmd + 5);
    } else if (strcmp(cmd, "close") == 0) {
        shell_close();
    } else if (strncmp(cmd, "touch ", 6) == 0) {
        shell_touch(cmd + 6);
    } else if (strncmp(cmd, "write ", 6) == 0) {
        shell_write(cmd + 6);
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        shell_rm(cmd + 3);
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        shell_cat(cmd + 4);
    } else if (strncmp(cmd, "run ", 4) == 0) {
        shell_run(cmd + 4);
    } else if (is_valid_python_syntax(cmd)) {
        if (is_file_open) {
            size_t cmd_len = strlen(cmd);
            if (active_buf_len + cmd_len + 2 < MAX_FILE_SIZE) {
                memcpy(active_buffer + active_buf_len, cmd, cmd_len);
                active_buf_len += cmd_len;
                active_buffer[active_buf_len++] = '\n';
                active_buffer[active_buf_len] = '\0';
            } else {
                for (const char *p = "\n[Warning] File buffer limit reached! Code executed without saving.\n"; *p; p++) {
                    kputc_color(*p, err_color);
                }
            }
        }
        execute_python_src(cmd);
    } else {
        for (const char *p = "\nCommand not found. Type 'help' for available commands.\n\n"; *p; p++) {
            kputc_color(*p, err_color);
        }
    }
}

void kernel_main(void) {
    vga_enable_cursor();
    
    play_boot_intro();

    init_vfs();
    draw_desktop_layout();

    void *sp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(sp));
    mp_stack_set_top(sp);
    
    mp_stack_set_limit(32768);

    gc_init(mp_heap, mp_heap + MP_HEAP_SIZE);

    nlr_buf_t nlr_init;
    if (nlr_push(&nlr_init) == 0) {
        mp_init();
        nlr_pop();
    } else {
        mp_hal_stdout_tx_strn("Init Exception Ignored.\n", 24);
    }

    char buffer[HIST_BUF_SIZE];
    int buf_len = 0;

    print_prompt();

    while (1) {
        vga_set_cursor(terminal_row, terminal_column);

        uint8_t key = get_key_event();

        if (key == KEY_ENTER) {
            buffer[buf_len] = '\0';
            kputc('\n');

            if (buf_len > 0) {
                handle_command(buffer);
            }

            buf_len = 0;
            buffer[0] = '\0';
            print_prompt();

        } else if (key == KEY_SHIFT_ENTER) {
            if (buf_len < HIST_BUF_SIZE - 1) {
                buffer[buf_len++] = '\n';
                mp_hal_stdout_tx_strn("\n... ", 5);
            }

        } else if (key == KEY_LEFT) {
            if (terminal_column > 1 + PROMPT_LEN) {
                terminal_column--;
            }

        } else if (key == KEY_RIGHT) {
            if (terminal_column < 1 + PROMPT_LEN + (size_t)buf_len) {
                terminal_column++;
            }

        } else if (key == KEY_BACKSPACE) {
            if (buf_len > 0) {
                buf_len--;
                buffer[buf_len] = '\0';
                kputc('\b');
            }

        } else if (key >= 32 && key <= 126) {
            if (buf_len < HIST_BUF_SIZE - 1) {
                buffer[buf_len++] = (char)key;
                kputc_color((char)key, input_color);
            }
        }
    }
}

void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    (void)expr; (void)file; (void)line; (void)func; while(1);
}
void __attribute__((noreturn)) _assert(const char *expr, const char *file, unsigned int line) {
    (void)expr; (void)file; (void)line; while(1);
}
void __attribute__((weak)) abort(void) { while(1); }