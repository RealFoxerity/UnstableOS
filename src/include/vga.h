#ifndef VGA_H
#define VGA_H
#include <stdint.h>

#define VGA_WIDTH 80  // VGA text mode 3, don't change
#define VGA_HEIGHT 25

#define VGA_TEXT_MODE_ADDR 0xB8000

#define VGA_INPUT_STATUS_1_REGISTER 0x3DA 

#define VGA_ATTRIBUTE_REGISTER_A_PORT 0x3C0 // write address - write data, or write address - read address, why? no clue
#define VGA_ATTRIBUTE_REGISTER_R_PORT 0x3C1 //
enum VGA_COLORS {
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GRAY = 7,
	VGA_COLOR_DARK_GRAY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
	VGA_COLOR_BLINK = 8
};

#define VGA_REGISTER_INDEX_PAS_BIT (0x20) // DO NOT FORGET TO SET, VGA WILL STOP WORKING
#define VGA_ATTRIBUTE_MODE_CONTROL (0x10 | VGA_REGISTER_INDEX_PAS_BIT) // PAS has to be enabled


enum mode_control_attribute_structure {
	MODE_CONTROL_IS_GRAPHICS = 1,
	MODE_CONTROL_IS_MONOCHROME__dummy = 2, // doesn't actually work
	MODE_CONTROL_LINE_GRAPHICS = 4, // In 9 bit character mode to "provide continuity" for chars 0xC0 - 0xDF, 1 -> 9th column = 8th column, else 9th column = background
	MODE_CONTROL_BLINK = 8,
	
	MODE_CONTROL_PPM = 32, /*
		This field allows the upper half of the screen to pan independently of the lower screen. If this field is set to 0 then nothing special occurs during a successful
		line compare (see the Line Compare field.) If this field is set to 1, then upon a successful line compare, the bottom portion of the screen is displayed as if the
		Pixel Shift Count and Byte Panning fields are set to 0. 
		*/
	MODE_CONTROL_8BIT = 64, // when set in 0x13 mode (256 colors) the video data is sampled for 8 bits of color
	MODE_CONTROL_P54S = 128, // "This bit selects the source for the P5 and P4 video bits that act as inputs to the video DAC. When this bit is set to 0, P5 and P4 are the
							 //  outputs of the Internal Palette registers. When this bit is set to 1, P5 and P4 are bits 1 and 0 of the Color Select register." 
};


void vga_write_attribute(uint8_t index, uint8_t data);
uint8_t vga_read_attribute(uint8_t index);



#include <stdint.h>
#include <stddef.h>

#define get_color(fg, bg) (fg | (bg<<4))

#define TAB_WIDTH 8

#define VGA_DEF_FG VGA_COLOR_LIGHT_GRAY
#define VGA_DEF_BG VGA_COLOR_BLACK

#define VGA_DEF_COLOR get_color(VGA_DEF_FG, VGA_DEF_BG)

struct vga_cursor_pos {
    uint8_t x,y;
};

void vga_clear();

struct vga_cursor_pos vga_get_cursor();
void vga_disable_cursor();
void vga_enable_cursor(uint8_t start_scanline, uint8_t end_scanline);
void vga_move_cursor(uint8_t x, uint8_t y);


void vga_enable_blink();
void vga_disable_blink();

uint8_t vga_get_color();
void vga_set_color(char vga_fg, char vga_bg);

void vga_put_char(char c, unsigned char color, uint8_t x, uint8_t y);
void vga_write(const char * s, size_t len);


#endif