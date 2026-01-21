#include <stddef.h>
#include <stdint.h>
#include "include/devs.h"
#include "include/kernel.h"
#include "include/kernel_tty_io.h"
#include "include/keyboard.h"
#include "include/vga.h"
#include "../libc/src/include/string.h"
#include "../libc/src/include/ctype.h"
#include "include/kernel_tty.h"
#include "include/lowlevel.h"

void vga_enable_blink() { // effectively removes the 16th background color (white)
    uint8_t current_attr = vga_read_attribute(VGA_ATTRIBUTE_MODE_CONTROL);
    current_attr |= MODE_CONTROL_BLINK;
    vga_write_attribute(VGA_ATTRIBUTE_MODE_CONTROL, current_attr);    
}
void vga_disable_blink() {
    uint8_t current_attr = vga_read_attribute(VGA_ATTRIBUTE_MODE_CONTROL);
    current_attr &= ~MODE_CONTROL_BLINK;
    vga_write_attribute(VGA_ATTRIBUTE_MODE_CONTROL, current_attr);    
}

#define MAX_SCANLINE 15
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) { // scanlines?  no clue

    outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);

	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);

}
void vga_disable_cursor() {
    outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}
void vga_move_cursor(uint8_t x, uint8_t y) {
    uint32_t interr_enabled = 0;
    asm volatile (
        "pushf\n\t"
        "andl $"STR(IA_32_EFL_SYSTEM_INTER_EN) ", (%%esp)\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(interr_enabled)
    );
    outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) ((y*VGA_WIDTH + x) & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) (((y*VGA_WIDTH + x) >> 8) & 0xFF));

    if (interr_enabled) asm volatile("sti;");
}


/*
    vga text format
    7   6 - 4   3 - 0   |   7 - 0
blink   BGC     FGC         ascii codepoint
*/
uint8_t vga_x = 0, vga_y = 0;

struct vga_cursor_pos vga_get_cursor() {
    return (struct vga_cursor_pos) {vga_x, vga_y};
}

#define get_char(char, color) ((uint16_t)(char | (color << 8)))

#define vga_buf ((uint16_t*)VGA_TEXT_MODE_ADDR)

static uint8_t vga_color = VGA_DEF_COLOR;

uint8_t vga_get_color() {return vga_color;}

void vga_set_color(char vga_fg, char vga_bg) {
    vga_color = get_color(vga_fg, vga_bg);
}

void vga_put_char(char c, uint8_t color, uint8_t x, uint8_t y) {
    vga_buf[y*VGA_WIDTH + x] = get_char(c, color);
}

void vga_clear() {
    vga_x = vga_y = 0;
    for (int i = 0; i < VGA_WIDTH; i++) {
        for (int j = 0; j < VGA_HEIGHT; j++) {
            vga_put_char(0, vga_color, i, j);
        }
    }
    vga_move_cursor(0, 0);
    vga_enable_cursor(0, MAX_SCANLINE);
}

static inline void vga_scroll_line() {
    memcpy(vga_buf, vga_buf + VGA_WIDTH, VGA_WIDTH*(VGA_HEIGHT-1)*sizeof(uint16_t)); // basic 1 line scrollback
    memset(vga_buf + (VGA_HEIGHT-1) * VGA_WIDTH, 0, VGA_WIDTH*sizeof(uint16_t));
}

void vga_write(const char * s, size_t len) {
    for (int i = 0; i < len; i++) {
        //if (s[i] == 0x7F) { // delete, should theoretically do nothing, commented out because it produces a nice house character :3
        //    continue;
        //}
        if (s[i] == '\r') {
            vga_x = 0; continue;
        }
        if (s[i] == '\b') {
            delete:
            if (vga_x == 0) {
                if (vga_y == 0) continue;
                vga_y -= 1;
                vga_x = VGA_WIDTH-1;
            } else {
                vga_x --;
            }
            vga_put_char(0, vga_color, vga_x, vga_y); // assuming cursor is in front of text
            if (vga_x == 0 && vga_y == 0) continue;
            if (vga_x == 0) {
                if ((((unsigned short *) VGA_TEXT_MODE_ADDR)[(vga_y - 1)*VGA_WIDTH + VGA_WIDTH - 1] & 0xFF) == 0) goto delete;
            } else {
                if ((((unsigned short *) VGA_TEXT_MODE_ADDR)[vga_y*VGA_WIDTH + vga_x - 1] & 0xFF) == 0) goto delete;
            }
            continue;
        }
        if (s[i] == '\t') {
            vga_x += TAB_WIDTH - (vga_x % TAB_WIDTH);
            if (vga_x >= VGA_WIDTH) {
                vga_x = 0;
                vga_y ++;
                if (vga_y >= VGA_HEIGHT) {
                    vga_y = VGA_HEIGHT - 1;
                    vga_scroll_line();
                }
            }
            continue;
        }
        if (s[i] == '\n') {
            vga_x = 0;
        
            //vga_y = (vga_y+1)%VGA_HEIGHT;
            vga_y ++;
            if (vga_y >= VGA_HEIGHT) {
                vga_y = VGA_HEIGHT - 1;
                vga_scroll_line();
            }
            continue;
        }
        vga_put_char(s[i], vga_color, vga_x, vga_y);
        vga_x ++;
        if (vga_x >= VGA_WIDTH) {
            vga_x = 0;
            
            //vga_y = (vga_y+1)%VGA_HEIGHT;
            vga_y ++;
            if (vga_y >= VGA_HEIGHT) {
                vga_y = VGA_HEIGHT - 1;
                vga_scroll_line();
            }
            continue;
        }
    }
    vga_move_cursor(vga_x, vga_y);
}



#define TTY_SHIFT_MOD_MASK 0x7F
#define TTY_OTHERS_START 87
static const char scancode_to_char[256] = {
    0, 0, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', '^', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*',
    'M', ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, '-', 0, 0, 0,'+', 0,
    0, 0, 0, 0x7F, 0, 0, 0,

    0, 0, '\n', '^', 0, 0, 0, 0, // keyboard_scan_code_set_1_others_translated, subtract from KEY_MULTIMEDIA_PREV_TRACK add TTY_OTHERS_START
    0, 0, 0, '/', 'M', '\r', 0, 0,
    0, 0, 0, 0, 0, 0, 0x7F, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,


    //shift
    
    0, 0, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', '^', 'A', 'S', 
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*',
    'M', ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0,

    0, 0, '\n', '^', 0, 0, 0, 0, // keyboard_scan_code_set_1_others_translated, subtract KEY_MULTIMEDIA_PREV_TRACK, add TTY_SHIFT_MOD_MASK, add TTY_OTHERS_START
    0, 0, 0, '/', 'M', '\r', 0, 0,
    0, 0, 0, 0, 0, 0, 0x7F, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
}; // currently shift = numlock just to test

void tty_console_input(uint32_t scancode) { // convert ps2 input into normal ascii for terminal use
    if (scancode & KEY_RELEASED_MASK) return;
    char is_shift = (scancode & KEY_MOD_LSHIFT_MASK || scancode & KEY_MOD_RSHIFT_MASK);

    if ((scancode & (KEY_MOD_RMETA_MASK-1)) >= KEY_MULTIMEDIA_PREV_TRACK) scancode = scancode - KEY_MULTIMEDIA_PREV_TRACK + TTY_OTHERS_START;

    char translated_scancode = 0;
   
    if (isalpha(scancode_to_char[(scancode & 0xFF)])) is_shift ^= (scancode & KEY_MOD_CAPSLOCK_MASK) != 0;  // capslock works only on alphabet characters
    
    translated_scancode = scancode_to_char[(scancode & 0xFF) + TTY_SHIFT_MOD_MASK*is_shift];

    if ((scancode & ~(KEY_MOD_RMETA_MASK-1)) == KEY_MOD_LCONTROL_MASK) { // if only holding ctrl (and or shift)
        if (toupper(translated_scancode) >= '@' && toupper(translated_scancode) <= '_') // if char between C0 values
            translated_scancode = toupper(translated_scancode) - '@'; // get C0 control char
        else {
            tty_write_to_tty("^", 1, GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE)); // wasn't a valid ctrl escape, printing the raw escape value
        }
    }
    if ((scancode & ~(KEY_MOD_RMETA_MASK-1) & ~(KEY_MOD_LSHIFT_MASK) & ~(KEY_MOD_RSHIFT_MASK)) == KEY_MOD_LCONTROL_MASK && translated_scancode == '?') translated_scancode = '\x7F'; // can't get to ? on english layout without shift
    if (translated_scancode != '\0') tty_write_to_tty(&translated_scancode, 1, GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE));
}


void console_write(tty_t * tty) {
    while (!EMPTY(&tty->oqueue)) {
        char checked = tty->oqueue.buffer[tty->oqueue.head+1];
        // TODO: implement ansi escape codes
        switch (checked) {
            
        }
    }
}