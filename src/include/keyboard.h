#ifndef KEYBOARD_SCAN_SET_1_H
#define KEYBOARD_SCAN_SET_1_H


#define KEY_RELEASED_MASK 0x80

#define KEY_MOD_NUMLOCK_MASK 0x200000
#define KEY_MOD_CAPSLOCK_MASK 0x100000
#define KEY_MOD_LSHIFT_MASK 0x80000
#define KEY_MOD_RSHIFT_MASK 0x40000
#define KEY_MOD_LCONTROL_MASK 0x20000
#define KEY_MOD_RCONTROL_MASK 0x10000
#define KEY_MOD_LALT_MASK 0x8000
#define KEY_MOD_RALT_MASK 0x4000
#define KEY_MOD_LMETA_MASK 0x2000
#define KEY_MOD_RMETA_MASK 0x1000 // keep it the lowest

#define KEY_BASE_SCANCODE_MASK 0xFFF

enum keyboard_scan_code_set_1 { // direct PS/2 scan codes
    KEY_INVALID = 0, // not a real key, just a dummy for when we recieve invalid data
    KEY_ESCAPE,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,
    KEY_MINUS,
    KEY_EQUAL,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_Q,
    KEY_W,
    KEY_E,
    KEY_R,
    KEY_T,
    KEY_Y,
    KEY_U,
    KEY_I,
    KEY_O,
    KEY_P,
    KEY_LBRACE, // [
    KEY_RBRACE, // ]
    KEY_ENTER,
    KEY_LCONTROL, // just the press and release, info for modifiers will be additionally provided
    KEY_A,
    KEY_S,
    KEY_D,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_BACKTICK,
    KEY_LSHIFT, // just the press and release, info for modifiers will be additionally provided
    KEY_BACKSLASH,
    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,
    KEY_N,
    KEY_M,
    KEY_COMMA,
    KEY_DOT,
    KEY_SLASH,
    KEY_RSHIFT, // just the press and release, info for modifiers will be additionally provided
    KEY_KP_ASTERISK,
    KEY_LALT, // just the press and release, info for modifiers will be additionally provided
    KEY_SPACE,
    KEY_CAPSLOCK,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_NUMLOCK,
    KEY_SCROLLLOCK,
    KEY_KP_7,
    KEY_KP_8,
    KEY_KP_9,
    KEY_KP_MINUS,
    KEY_KP_4,
    KEY_KP_5,
    KEY_KP_6,
    KEY_KP_PLUS,
    KEY_KP_1,
    KEY_KP_2,
    KEY_KP_3,
    KEY_KP_0,
    KEY_KP_DOT,
    KEY_SYSRQ, // legacy sysrq, modern is E0 37 
    KEY_F11 = 0x57,
    KEY_F12,
};
enum keyboard_scan_code_set_1_others_translated { // beware to not use the KEY_RELEASED_MASK bit
    KEY_MULTIMEDIA_PREV_TRACK = 0x100, // keep first
    KEY_MULTIMEDIA_NEXT_TRACK,
    KEY_KP_ENTER,
    KEY_RCONTROL, // just the press and release, info for modifiers will be additionally provided
    KEY_MULTIMEDIA_MUTE,
    KEY_MULTIMEDIA_CALCULATOR,
    KEY_MULTIMEDIA_PLAY,
    KEY_MULTIMEDIA_STOP,
    KEY_VOLUME_DOWN,
    KEY_VOLUME_UP,
    KEY_WWW_HOME,
    KEY_KP_SLASH,
    KEY_RALT, // just the press and release, info for modifiers will be additionally provided
    KEY_HOME,
    KEY_UP,
    KEY_PAGE_UP,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_END,
    KEY_DOWN,
    KEY_PAGE_DOWN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_LMETA, // just the press and release, info for modifiers will be additionally provided
    KEY_RMETA, // just the press and release, info for modifiers will be additionally provided
    KEY_COMPOSE,
    KEY_ACPI_POWER,
    KEY_ACPI_SLEEP,
    KEY_ACPI_WAKE,
    KEY_MULTIMEDIA_WWW_SEARCH,
    KEY_MULTIMEDIA_WWW_FAVORITES,
    KEY_MULTIMEDIA_WWW_REFRESH,
    KEY_MULTIMEDIA_WWW_STOP,
    KEY_MULTIMEDIA_WWW_FORWARD,
    KEY_MULTIMEDIA_WWW_BACK,
    KEY_MULTIMEDIA_MY_COMPUTER,
    KEY_MULTIMEDIA_EMAIL,
    KEY_MULTIMEDIA_MEDIA_SELECT,
    
    KEY_PRINT_SCREEN = 0x200,
    KEY_PAUSE = 0x300,
    KEY_BREAK = 0x400
};

#include <stdint.h>

// kernel_ps2.c

char is_scancode_printable(uint32_t scancode);
uint32_t scancode_translate_numpad(uint32_t scancode); // translates chars from KP_* to either numbers or special chars based on numlock state, kernel_ps2.c
char is_scancode_mod(uint32_t scancode); // if the scancode is a modifier (shift/alt/ctrl/meta)
#endif