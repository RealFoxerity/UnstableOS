#include <stddef.h>
#include <stdint.h>
#include "../libc/src/include/stdio.h"
#include "include/devs.h"
#include "include/kernel.h"
#include "include/kernel_tty_io.h"
#include "include/keyboard.h"
#include "gfx.h"
#include "../libc/src/include/string.h"
#include "../libc/src/include/ctype.h"
#include "include/kernel_console.h"

#include "stdlib.h"


enum console_colors_palette console_color_fg         = CONSOLE_COLOR_WHITE;
enum console_colors_palette console_color_bg         = CONSOLE_COLOR_BLACK;
enum console_colors_palette console_default_color_fg = CONSOLE_COLOR_WHITE;
enum console_colors_palette console_default_color_bg = CONSOLE_COLOR_BLACK;

char console_reversed_colors = 0;

// if this, then delete chars until (and including) encountering normal character
#define CHAR_CELL_RELATED (-1)
struct character_cell {
    char c;
    enum console_colors_palette fg;
    enum console_colors_palette bg;
};

int console_x = 0; // signed to make bounds checking easier
int console_y = 0;

int console_saved_x = 0;
int console_saved_y = 0;

#define CONSOLE_CURSOR_CHAR ' '
#define CONSOLE_CURSOR_BG CONSOLE_COLOR_WHITE
#define CONSOLE_CURSOR_FG CONSOLE_COLOR_BLACK

char console_cursor_shown = 1;
char console_cursor_blinking = 1;

int console_cursor_x = 0;
int console_cursor_y = 0;

unsigned char console_tab_width = 8;

#define MAX_ANSI_SEQUENCE 16

#define FONT_MULTIPLIER 1

int console_buffer_w = 0;
int console_buffer_h = 0;
struct character_cell * console_buffer = NULL;
spinlock_t console_buffer_lock = {0};

char cursor_busy = 0;
char cursor_currently_visible = 0;

void console_cursor_hide() {
    if (!cursor_busy && cursor_currently_visible) {
        // because we run these from the RTC interrupt, we could be in the VGA draw routine -> deadlock
        // it would be much better to have a separate thread for blinking
        // but this since the cursor isn't that important, we can just skip drawing
        extern spinlock_t gfx_spinlock;
        if (gfx_spinlock.state == SPINLOCK_LOCKED) return;

        cursor_busy = 1;
        cursor_currently_visible = 0;

        if (current_process == NULL || console_buffer == NULL) {
            gfx_blit_char(
               ' ',
               console_cursor_x * console_font_width * FONT_MULTIPLIER,
               console_cursor_y * console_font_height * FONT_MULTIPLIER,
               console_default_color_fg, console_default_color_bg,
               1, FONT_MULTIPLIER);
            return;
        }


        spinlock_acquire(&console_buffer_lock);

        if (console_cursor_y >= console_buffer_h ||
            console_cursor_x >= console_buffer_w ||
            console_cursor_y < 0 ||
            console_cursor_x < 0
        ) {
            spinlock_release(&console_buffer_lock);
            return;
        }

        struct character_cell restored = console_buffer[console_cursor_y * console_buffer_w + console_cursor_x];
        gfx_blit_char(
           restored.c,
           console_cursor_x * console_font_width * FONT_MULTIPLIER,
           console_cursor_y * console_font_height * FONT_MULTIPLIER,
           restored.fg, restored.bg,
           1, FONT_MULTIPLIER);
        spinlock_release(&console_buffer_lock);
        cursor_busy = 0;
    }
}
void console_cursor_show() {
    if (!cursor_busy && !cursor_currently_visible) {

        extern spinlock_t gfx_spinlock;
        if (gfx_spinlock.state == SPINLOCK_LOCKED) return;

        cursor_busy = 1;
        cursor_currently_visible = 1;

        gfx_blit_char(
            CONSOLE_CURSOR_CHAR,
            console_cursor_x * console_font_width * FONT_MULTIPLIER,
            console_cursor_y * console_font_height * FONT_MULTIPLIER,
            CONSOLE_CURSOR_FG, CONSOLE_CURSOR_BG,
            1, FONT_MULTIPLIER);
        cursor_busy = 0;
    }
}

// called from the RTC handler
#define CONSOLE_CURSOR_REFRESH_TICKS 256
void console_blink_cursor() {
    if (uptime_clicks % CONSOLE_CURSOR_REFRESH_TICKS) return;
    if (!console_cursor_shown) {
        console_cursor_hide();
        return;
    }

    static char status = 0;

    if (console_cursor_blinking) {
        status = !status;
        if (status) console_cursor_hide();
        else console_cursor_show();
    } else {
        console_cursor_show();
    }
}

void console_move_cursor(int x, int y) {
    console_cursor_hide();
    cursor_busy = 1;

    console_cursor_y = y;
    console_cursor_x = x;

    cursor_busy = 0;
}
void console_redraw_range(int startx, int endx, int starty, int endy) {
    spinlock_acquire(&console_buffer_lock);

    if (startx >= console_buffer_w ||
        starty >= console_buffer_h) {
        spinlock_release(&console_buffer_lock);
        return;
    }

    if (endx >= console_buffer_w) endx = console_buffer_w - 1;
    if (endy >= console_buffer_h) endy = console_buffer_h - 1;

    for (unsigned int y = starty; y <= endy; y++) {
        for (unsigned int x = startx; x <= endx; x++) {
            //if (!isprint(console_buffer[y * console_buffer_w + x].c)) continue;

            gfx_blit_char(
                console_buffer[y * console_buffer_w + x].c,
                x * console_font_width * FONT_MULTIPLIER, y * console_font_height * FONT_MULTIPLIER,
                console_buffer[y * console_buffer_w + x].fg,
                console_buffer[y * console_buffer_w + x].bg,
                1,
                FONT_MULTIPLIER
            );
        }
    }

    spinlock_release(&console_buffer_lock);
}

static void console_set_char(int x, int y, char c) {
    if (x >= display_width_chars / FONT_MULTIPLIER) return;
    if (y >= display_height_chars/ FONT_MULTIPLIER) return;

    static char panicked_here = 0;
    char redraw = 0;

    if (isprint(c)) {
        gfx_blit_char(c, console_x*console_font_width*FONT_MULTIPLIER,
                console_y*console_font_height*FONT_MULTIPLIER,
        console_color_fg, console_color_bg, 1, FONT_MULTIPLIER);
    }

    if (current_process == NULL || panicked_here) return; // scheduler not initialized -> early boot -> no heap

    spinlock_acquire_interruptible(&console_buffer_lock);

    if (console_buffer == NULL) { // early boot
        console_buffer =
            kalloc(sizeof(struct character_cell) *
                display_width_chars * display_height_chars /
                FONT_MULTIPLIER / FONT_MULTIPLIER);
        if (console_buffer == NULL) {
            panicked_here = 1; // so we don't deadlock on panic with spinlock_acquire
            panic("Failed to allocate console canvas!\n");
        }
        memset(console_buffer, 0, sizeof(struct character_cell) *
                display_width_chars * display_height_chars /
                FONT_MULTIPLIER / FONT_MULTIPLIER);

        console_buffer_w = display_width_chars / FONT_MULTIPLIER;
        console_buffer_h = display_height_chars / FONT_MULTIPLIER;

    } else if (console_buffer_w != display_width_chars / FONT_MULTIPLIER || // resolution changed
        console_buffer_h != display_height_chars / FONT_MULTIPLIER
    ) {
        struct character_cell * console_buffer_2 =
            kalloc(sizeof(struct character_cell) *
                display_width_chars * display_height_chars /
                FONT_MULTIPLIER / FONT_MULTIPLIER);
        if (console_buffer_2 == NULL) {
            panicked_here = 1; // so we don't deadlock on panic with spinlock_acquire
            panic("Failed to allocate console canvas!\n");
        }

        memset(console_buffer_2, 0, sizeof(struct character_cell) *
                display_width_chars * display_height_chars /
                FONT_MULTIPLIER / FONT_MULTIPLIER);

        int end_y = console_buffer_h - display_height_chars / FONT_MULTIPLIER;
        if (end_y < 0) end_y = 0;

        int new_line_length = console_buffer_w;
        if (new_line_length > display_width_chars / FONT_MULTIPLIER)
            new_line_length = display_width_chars / FONT_MULTIPLIER;

        for (
            int cy = console_buffer_h - 1;
                cy >= end_y;
                cy--
        ) {
            memcpy(console_buffer_2 + cy * display_width_chars / FONT_MULTIPLIER,
                   console_buffer + cy * console_buffer_w,
                   new_line_length * sizeof(struct character_cell)
            );
        }


        kfree(console_buffer);
        console_buffer = console_buffer_2;
        console_buffer_w = display_width_chars / FONT_MULTIPLIER;
        console_buffer_h = display_height_chars / FONT_MULTIPLIER;
        redraw = 1;
    }

    if (x >= console_buffer_w || y >= console_buffer_h) {
        spinlock_release(&console_buffer_lock);
        return;
    }

    console_buffer[y * console_buffer_w + x] = (struct character_cell) {
        .c = c,
        .fg = console_color_fg,
        .bg = console_color_bg
    };

    spinlock_release(&console_buffer_lock);

    if (redraw)
        console_redraw_range(0, console_buffer_w - 1, 0, console_buffer_h - 1);
}

static void console_scroll(int lines) {
    console_cursor_hide();
    cursor_busy = 1;
    gfx_hw_scroll_scanlines(console_font_height*FONT_MULTIPLIER);
    if (current_process == NULL || console_buffer == NULL) return; // scheduler not initialized -> early boot -> no heap

    spinlock_acquire(&console_buffer_lock);
    memcpy(console_buffer,
        console_buffer + lines * console_buffer_w,
        (console_buffer_h - lines) * console_buffer_w * sizeof(struct character_cell)
    );
    memset(console_buffer + (console_buffer_h - lines) * console_buffer_w,
        0,
        lines * console_buffer_w * sizeof(struct character_cell)
    );
    spinlock_release(&console_buffer_lock);
    cursor_busy = 0;
}

static void handle_ansi_escapes(const char * ansi_sequence) {
    /*
    this is a pretty simple implementation with a lot missing
    we support:
    most common SGR type (\e[<num>m) escapes
    cursor manipulation types A, B, C, D, E, F, G, H, a, d, f, j, k
    cursor blink off/on
    clear screen type J/0J 1J 2J 3J, K/0K 1K 2K
    scroll commands S T
    */
    switch (ansi_sequence[1]) { // 1 char escapes
        case 'm':
            reset_graphics:
            console_color_fg = console_default_color_fg;
            console_color_bg = console_default_color_bg;
            console_reversed_colors = 0;
            return;
        case 'J':
            erase_cursor_screen_end:

            if (current_process && console_buffer != NULL) {
                spinlock_acquire(&console_buffer_lock);
                if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;
                if (console_x >= console_buffer_w) console_x = console_buffer_w - 1;

                if (console_y < 0) console_y = 0;
                if (console_x < 0) console_x = 0;

                memset(console_buffer + console_y * console_buffer_w + console_x,
                    0,
                    (console_buffer_w - console_x) * sizeof(struct character_cell) +
                    (console_buffer_h - console_y - 1) * console_buffer_w * sizeof(struct character_cell)
                );
                spinlock_release(&console_buffer_lock);
            }

            current_video_funcs->fill_buffered(0, display_width - 1,
                (console_y+1)*console_font_width*FONT_MULTIPLIER, display_height - 1,
                console_color_bg, 1);
            current_video_funcs->swap_region(0, display_width - 1,
                (console_y+1)*console_font_width*FONT_MULTIPLIER, display_height - 1);
        case 'K':
            erase_cursor_line_end:
            if (current_process && console_buffer != NULL) {
                spinlock_acquire(&console_buffer_lock);
                if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;
                if (console_x >= console_buffer_w) console_x = console_buffer_w - 1;

                if (console_y < 0) console_y = 0;
                if (console_x < 0) console_x = 0;

                memset(console_buffer + console_y * console_buffer_w + console_x,
                    0,
                    (console_buffer_w - console_x) * sizeof(struct character_cell)
                );
                spinlock_release(&console_buffer_lock);
            }

            current_video_funcs->fill_buffered(console_x*console_font_width*FONT_MULTIPLIER, display_width - 1,
                    console_y*console_font_height*FONT_MULTIPLIER, (console_y+1)*console_font_height*FONT_MULTIPLIER - 1,
                    console_color_bg, 1);
            current_video_funcs->swap_region(console_x*console_font_width*FONT_MULTIPLIER, display_width - 1,
                    console_y*console_font_height*FONT_MULTIPLIER, (console_y+1)*console_font_height*FONT_MULTIPLIER - 1);
            return;
        case 's':
            console_saved_x = console_x;
            console_saved_y = console_y;
            return;
        case 'u':
            console_x = console_saved_x % (display_width_chars/FONT_MULTIPLIER);
            console_y = console_saved_y % (display_height_chars/FONT_MULTIPLIER);
            return;
        case 'H':
            console_x = console_y = 0;
            return;
        case 'A':
            if (console_y > 0) console_y --;
            return;
        case 'B':
            if (console_y < display_height_chars/FONT_MULTIPLIER - 1) console_y ++;
            return;
        case 'C':
            if (console_x < display_width_chars/FONT_MULTIPLIER - 1) console_x ++;
            return;
        case 'D':
            if (console_x > 0) console_x --;
            return;
    }

    char csi = 0;
    int id = 0, id2 = 0;

    if (sscanf(ansi_sequence, "[?%u%c", &id, &csi) == 2) {
        switch (id) {
            case 12:
                switch (csi) {
                    case 'h':
                        console_cursor_blinking = 1;
                        return;
                    case 'l':
                        console_cursor_blinking = 0;
                    default:
                        return;
                }
            case 25:
                switch (csi) {
                case 'h':
                        console_cursor_shown = 1;
                        return;
                case 'l':
                        console_cursor_shown = 0;
                default:
                        return;
                }
        }
        return;
    }

    if (sscanf(ansi_sequence, "[%d%c", &id, &csi) != 2) goto ansi2params;

    enum console_colors_palette temp;
    switch (csi) {
        // scroll up
        case 'S':
            // cannot hw scroll because we'd uncover previous data
            // we could then zerofill but at that point might as well just copy
            if (current_process && console_buffer != NULL) {
                spinlock_acquire(&console_buffer_lock);
                if (id > console_buffer_h) id = console_buffer_h;

                memcpy(console_buffer,
                    console_buffer + id * console_buffer_w,
                        (console_buffer_h - id) * console_buffer_w * sizeof(struct character_cell)
                    );
                memset(console_buffer + (console_buffer_h - id) * console_buffer_w,
                    0,
                    id * console_buffer_w * sizeof(struct character_cell)
                );
                spinlock_release(&console_buffer_lock);
            }
            current_video_funcs->copy_region_unbuffered(0, id*console_font_height*FONT_MULTIPLIER, display_width, display_height, 0, 0);
            current_video_funcs->fill_buffered(
                0, display_width - 1,
                (console_buffer_h-id)*console_font_height*FONT_MULTIPLIER, id*console_font_height*FONT_MULTIPLIER - 1,
                0, 0
            );
            current_video_funcs->swap_region(
                0, display_width - 1,
                (console_buffer_h-id)*console_font_height*FONT_MULTIPLIER, id*console_font_height*FONT_MULTIPLIER - 1
            );
            return;
        // scroll down
        case 'T':
            if (current_process && console_buffer != NULL) {
                spinlock_acquire(&console_buffer_lock);
                if (id > console_buffer_h) id = console_buffer_h;

                if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;

                memmove(console_buffer + id * console_buffer_w,
                    console_buffer,
                        (console_buffer_h - id) * console_buffer_w * sizeof(struct character_cell)
                    );
                memset(console_buffer,
                    0,
                    id * console_buffer_w * sizeof(struct character_cell)
                );

                if (console_y < console_buffer_h) {
                    memset(console_buffer + (console_y + 1) * console_buffer_w,
                        0,
                        (console_buffer_h - console_y - 1) * console_buffer_w * sizeof(struct character_cell)
                    );
                }
                spinlock_release(&console_buffer_lock);
            }

            current_video_funcs->copy_region_unbuffered(0, 0, display_width, display_height, 0, id*console_font_height*FONT_MULTIPLIER);
            current_video_funcs->fill_buffered(
                0, display_width - 1,
                0, id*console_font_height*FONT_MULTIPLIER - 1,
                0, 0
            );
            current_video_funcs->fill_buffered(
                0, display_width - 1,
                (console_y + 1)*console_font_height*FONT_MULTIPLIER, display_height - 1,
                0, 0
            );

            current_video_funcs->swap_region(0, display_width - 1,
                0, id*console_font_height*FONT_MULTIPLIER - 1);
            current_video_funcs->swap_region(0, display_width - 1,
                (console_y + 1)*console_font_width*FONT_MULTIPLIER, display_height - 1);
            return;
        // graphics rendition modes
        case 'm':
            switch (id) {
                case 0: goto reset_graphics;
                case 7:
                    // so we don't have to do ifs in our drawing code
                    temp = console_color_bg;
                    console_color_bg = console_color_fg;
                    console_color_fg = temp;
                    console_reversed_colors = 1;
                    return;
                case 27:
                    if (!console_reversed_colors) return;
                    temp = console_color_bg;
                    console_color_bg = console_color_fg;
                    console_color_fg = temp;
                    return;
                case 30 ... 37:
                    console_color_fg = id - 30;
                    return;
                case 39:
                    console_color_fg = console_default_color_fg;
                    return;
                case 40 ... 47:
                    console_color_bg = id - 40;
                    return;
                case 49:
                    console_color_bg = console_default_color_bg;
                    return;
                case 90 ... 97:
                    console_color_fg = id - 90 + 8;
                    return;
                case 100 ...107:
                    console_color_bg = id - 90 + 8;
                    return;
            }
            return;

        // clear screen
        case 'J':
            switch (id) {
                case 0: goto erase_cursor_screen_end;
                case 1:
                    // erase from beginning to cursor, TODO: maybe use vga_fill?
                    if (current_process && console_buffer != NULL) {
                        spinlock_acquire(&console_buffer_lock);
                        if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;
                        if (console_x >= console_buffer_w) console_x = console_buffer_w - 1;

                        if (console_y < 0) console_y = 0;
                        if (console_x < 0) console_x = 0;

                        memset(console_buffer,
                            0,
                            console_y * console_buffer_w * sizeof(struct character_cell) +
                            console_x * sizeof(struct character_cell)
                        );
                        spinlock_release(&console_buffer_lock);
                    }
                    current_video_funcs->fill_buffered(
                        0, display_width - 1,
                        0, (console_y - 1) * console_font_height * FONT_MULTIPLIER - 1,
                        0, 0);
                    current_video_funcs->fill_buffered(
                        0, console_x * console_font_width * FONT_MULTIPLIER - 1,
                        console_y * console_font_height * FONT_MULTIPLIER, (console_y + 1) * console_font_height * FONT_MULTIPLIER - 1,
                        0, 0);

                    current_video_funcs->swap_region(
                        0, display_width - 1,
                        0, (console_y + 1) * console_font_height * FONT_MULTIPLIER - 1);
                    return;
                case 3:
                    // erase scrollback (in our case do nothing)
                    break;
                case 2:
                    // entire screen
                    if (current_process && console_buffer != NULL) {
                        spinlock_acquire(&console_buffer_lock);
                        memset(console_buffer, 0, console_buffer_w * console_buffer_h * sizeof(struct character_cell));
                        spinlock_release(&console_buffer_lock);
                    }
                    current_video_funcs->fill_buffered(0, display_width - 1,
                        0, display_height - 1,
                        console_color_bg, 1);
                    gfx_swap_buffers();
                    return;
            }
            return;
        // clear line
        case 'K':
            switch (id) {
                case 0: goto erase_cursor_line_end;
                case 1:
                    // erase from cursor to beginning of line
                    if (current_process && console_buffer != NULL) {
                        spinlock_acquire(&console_buffer_lock);
                        if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;
                        if (console_x >= console_buffer_w) console_x = console_buffer_w - 1;

                        if (console_y < 0) console_y = 0;
                        if (console_x < 0) console_x = 0;

                        memset(console_buffer + console_y * console_buffer_w,
                            0,
                            console_x * sizeof(struct character_cell)
                        );
                        spinlock_release(&console_buffer_lock);
                    }
                    current_video_funcs->fill_buffered(0, console_x * console_font_width * FONT_MULTIPLIER,
                        console_y*console_font_height*FONT_MULTIPLIER, (console_y+1)*console_font_height*FONT_MULTIPLIER - 1,
                        console_color_bg, 1);
                    current_video_funcs->swap_region(0, console_x * console_font_width * FONT_MULTIPLIER,
                        console_y*console_font_height*FONT_MULTIPLIER, (console_y+1)*console_font_height*FONT_MULTIPLIER - 1);
                    return;
                case 2:
                    // erase entire line
                    if (current_process && console_buffer != NULL) {
                        spinlock_acquire(&console_buffer_lock);
                        if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;
                        if (console_x >= console_buffer_w) console_x = console_buffer_w - 1;

                        memset(console_buffer + console_y * console_buffer_w,
                            0,
                            console_buffer_w * sizeof(struct character_cell)
                        );
                        spinlock_release(&console_buffer_lock);
                    }
                    current_video_funcs->fill_buffered(0, display_width - 1,
                        console_y*console_font_height*FONT_MULTIPLIER, (console_y+1)*console_font_height*FONT_MULTIPLIER - 1,
                        console_color_bg, 1);
                    current_video_funcs->swap_region(0, display_width - 1,
                        console_y*console_font_height*FONT_MULTIPLIER, (console_y+1)*console_font_height*FONT_MULTIPLIER - 1);
                    return;
            }
            return;
        // cursor operations
        case 'F':
            console_x = 0;
        case 'A':
            if (console_y > id) console_y -= id;
            else console_y = 0;
            return;
        case 'E':
            console_x = 0;
        case 'a':
        case 'B':
            if (console_y + id > display_height_chars/FONT_MULTIPLIER - 1)
                console_y = display_height_chars/FONT_MULTIPLIER - 1;
            else console_y += id;
            return;
        case 'k':
        case 'C':
            if (console_x + id > display_width_chars/FONT_MULTIPLIER - 1)
                console_x = display_width_chars/FONT_MULTIPLIER - 1;
            else console_x += id;
            return;
        case 'j':
        case 'D':
            if (console_x > id) console_x -= id;
            else console_x = 0;
            return;
        case 'G':
            if (id > display_width_chars/FONT_MULTIPLIER - 1)
                console_x = display_width_chars/FONT_MULTIPLIER - 1;
            else console_x = id;
            return;
        case 'd':
            if (id > display_height_chars/FONT_MULTIPLIER - 1)
                console_y = display_height_chars/FONT_MULTIPLIER - 1;
            else console_y = id;
            return;
        default:
            return;
    }

    ansi2params:
    if (sscanf(ansi_sequence, "[=%u;%u%c", &id, &id2, &csi) != 3) return;

    switch (csi) {
        // absolute cursor positioning
        case 'H':
        case 'f':
            if (id > display_height_chars/FONT_MULTIPLIER - 1)
                console_y = display_height_chars/FONT_MULTIPLIER - 1;
            else console_y = id;
            if (id2 > display_width_chars/FONT_MULTIPLIER - 1)
                console_x = display_width_chars/FONT_MULTIPLIER - 1;
            else console_x = id2;
            return;
    }

}

static char ansi_escape_state_machine(char c) {
    static char waiting = 0;
    static int read_chars = -1;
    static char sequence[MAX_ANSI_SEQUENCE+1] = {0};
    if (c == '\e') {
        waiting = 1;
        if (read_chars != -1)
            memset(sequence, 0, read_chars);
        read_chars = -1;
        return 1;
    }
    if (waiting) {
        if (read_chars == -1) {
            if (c != '[') { // we don't support the other ones
                waiting = 0;
                return 0;
            }
            read_chars = 0;
        }

        // probably broken/partially dropped sequence - give up
        if (read_chars >= MAX_ANSI_SEQUENCE || !isprint(c)) {
            waiting = 0;
            return 0;
        }

        switch (c) {
            case 'A'...'Z':
            case 'a'...'z':
            case '@':
            case '`':
                // the termination char
                waiting = 2;
            default:
                sequence[read_chars++] = c;
        }
        if (waiting == 2) {
            sequence[read_chars] = 0;
            waiting = 0;
            handle_ansi_escapes(sequence);
        }
        return 1;
    }
    return 0;
}

void console_write(const char * s, size_t len) {
    console_cursor_hide();
    cursor_busy = 1;
    for (int i = 0; i < len; i++) {
        if (ansi_escape_state_machine(s[i])) continue;

        if (s[i] == '\r') {
            console_x = 0; continue;
        }
        if (s[i] == '\b' /*|| s[i] == 0x7F*/) { // TODO: there may be a race condition with kernel/user printf
            if (!current_process || console_buffer == NULL) continue; // early boot, kprintf doesn't do \b anyway

            spinlock_acquire(&console_buffer_lock);

            if (console_y >= console_buffer_h) console_y = console_buffer_h - 1;
            if (console_x >= console_buffer_w) console_x = console_buffer_w - 1;

            if (console_y < 0) console_y = 0;
            if (console_x < 0) console_x = 0;
            if (console_x == 0 && console_y == 0) {
                spinlock_release(&console_buffer_lock);
                continue;
            }

            if (console_buffer[console_y * console_buffer_w + console_x - 1].c != CHAR_CELL_RELATED) {
                if (console_x == 0) {
                    if (console_y == 0) break;
                    console_y -= 1;
                    console_x = console_buffer_w - 1;
                } else {
                    console_x --;
                }
                console_buffer[console_y * console_buffer_w + console_x].c = 0;
                spinlock_release(&console_buffer_lock);
                gfx_blit_char(' ',
                    console_x * console_font_width * FONT_MULTIPLIER,
                    console_y * console_font_height * FONT_MULTIPLIER,
                    0, 0, 1, FONT_MULTIPLIER);
                continue;
            }
            while (console_buffer[console_y * console_buffer_w + console_x - 1].c == CHAR_CELL_RELATED) {
                if (console_x == 0) {
                    if (console_y == 0) break;
                    console_y -= 1;
                    console_x = console_buffer_w - 1;
                } else {
                    console_x --;
                }
                console_buffer[console_y * console_buffer_w + console_x].c = 0;
                gfx_blit_char(' ',
                    console_x * console_font_width * FONT_MULTIPLIER,
                    console_y * console_font_height * FONT_MULTIPLIER,
                    0, 0, 1, FONT_MULTIPLIER);
            }
            spinlock_release(&console_buffer_lock);
            ///*if (s[i] == 0x7F)*/ vga_put_char(0, vga_color, vga_x, vga_y); // assuming cursor is in front of text
            continue;
        }
        if (s[i] == '\n' || s[i] == '\v') { // see the vt102 user guide for \v behavior
            new_line:
            console_x = 0;
            if (console_y >= display_height_chars / FONT_MULTIPLIER - 1) {
                console_y = display_height_chars / FONT_MULTIPLIER - 1;
                console_scroll(1);
                current_video_funcs->fill_buffered(0, display_width - 1,
                    display_height - console_font_height*FONT_MULTIPLIER, display_height - 1,
                    console_default_color_bg, 1);
                current_video_funcs->swap_region(0, display_width - 1,
                    display_height - console_font_height*FONT_MULTIPLIER, display_height - 1);
            } else console_y++;
            continue;
        }
        if (s[i] == '\t') {
            int old_delta = console_tab_width - (console_x % console_tab_width);
            for (int i = 0; i < old_delta; i++) {
                console_write(&(char){CHAR_CELL_RELATED}, 1);
            }
            continue;
        }
        console_set_char(console_x, console_y, s[i]);
        if (console_x >= display_width_chars / FONT_MULTIPLIER - 1) goto new_line;
        console_x++;
    }
    console_move_cursor(console_x, console_y);
    cursor_busy = 0;
}



#define TTY_SHIFT_MOD_MASK 0x7F
#define TTY_OTHERS_START 87

// note: backspace is technically \b, but we do 0x7F in accordance to default VERASE on most POSIX systems, delete is up to userspace
static const char scancode_to_char[256] = {
    0, 0, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', 0x7F, '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', '^', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*',
    'M', ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, '-', 0, 0, 0,'+', 0,
    0, 0, 0, '\x7f', 0, 0, 0,

    0, 0, '\n', '^', 0, 0, 0, 0, // keyboard_scan_code_set_1_others_translated, subtract from KEY_MULTIMEDIA_PREV_TRACK add TTY_OTHERS_START
    0, 0, 0, '/', 'M', '\r', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,


    //shift

    0, 0, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', 0x7f, '\t',
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
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
}; // currently shift = numlock just to test

void tty_console_input_terminal_input_seq(uint32_t scancode) { // vt and partial xterm escaping
    char mods = 1;
    if (scancode & KEY_MOD_LSHIFT_MASK || scancode & KEY_MOD_RSHIFT_MASK) mods += 1;
    if (scancode & KEY_MOD_LALT_MASK /* || scancode & KEY_MOD_RALT_MASK */) mods += 2; // https://en.wikipedia.org/wiki/ANSI_escape_code#Terminal_input_sequences
    if (scancode & KEY_MOD_LCONTROL_MASK || scancode & KEY_MOD_RCONTROL_MASK) mods += 4;
    if (scancode & KEY_MOD_LMETA_MASK || scancode & KEY_MOD_RMETA_MASK) mods += 8;

    scancode = scancode & KEY_BASE_SCANCODE_MASK;
    if (scancode == 0) return; // probably recieved a modifier key press

    char esc_val = 0;

    switch (scancode) {
        // key f13-f20 + blank
        case KEY_F12: esc_val++;
        case KEY_F11: esc_val+=2;
        case KEY_F10: esc_val++;
        case KEY_F9: esc_val++;
        case KEY_F8: esc_val++;
        case KEY_F7: esc_val++;
        case KEY_F6: esc_val+=2;
        case KEY_F5: esc_val++;
        case KEY_F4: esc_val++;
        case KEY_F3: esc_val++;
        case KEY_F2: esc_val++;
        case KEY_F1: esc_val++;
        /* case KEY_F0: ???? */ esc_val +=2;
        /* case KEY_END: #2 */ esc_val ++;
        /* case KEY_HOME: #2 */ esc_val ++;
        case KEY_PAGE_DOWN: esc_val ++;
        case KEY_PAGE_UP: esc_val ++;
        case KEY_END: esc_val ++;
        case KEY_DELETE: esc_val ++;
        case KEY_INSERT: esc_val ++;
        case KEY_HOME: esc_val ++;
            break;

        case KEY_LEFT: esc_val ++;
        case KEY_RIGHT: esc_val ++;
        case KEY_DOWN: esc_val ++;
        case KEY_UP: esc_val += 'A';
            break;

        default: return;
    }


    char esc_buf[16] = {0};

    if (mods == 1)
        sprintf(esc_buf, esc_val >= 'A' ? "\e[%c"   : "\e[%d~", esc_val);
    else
        if (esc_val >= 'A') {
            if (!mods)
                sprintf(esc_buf, "\e[%c", esc_val);
            else
                sprintf(esc_buf, "\e[%d%c", mods, esc_val);
        }
        else
            sprintf(esc_buf, "\e[%d;%d~", esc_val, mods);

    tty_write_to_tty(esc_buf, strlen(esc_buf), GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE));
}

void console_translate_scancode(uint32_t scancode) { // convert ps2/com input into normal ascii for terminal use
    if (scancode & KEY_RELEASED_MASK) return;
    scancode = scancode_translate_numpad(scancode);

    if (!is_scancode_printable(scancode)) {
        tty_console_input_terminal_input_seq(scancode);
        return;
    }

    char is_shift = (scancode & KEY_MOD_LSHIFT_MASK || scancode & KEY_MOD_RSHIFT_MASK);

    if ((scancode & KEY_BASE_SCANCODE_MASK) >= KEY_MULTIMEDIA_PREV_TRACK) scancode = scancode - KEY_MULTIMEDIA_PREV_TRACK + TTY_OTHERS_START; // readjust scancode to lookup table

    char translated_scancode = 0;
    char explicit_nullbyte = 0; // if we really do want to output a null byte

    if (isalpha(scancode_to_char[(scancode & 0xFF)])) is_shift ^= (scancode & KEY_MOD_CAPSLOCK_MASK) != 0;  // capslock works only on alphabet characters

    translated_scancode = scancode_to_char[(scancode & 0xFF) + TTY_SHIFT_MOD_MASK*is_shift];

    if ((scancode & ~KEY_BASE_SCANCODE_MASK) == KEY_MOD_LCONTROL_MASK) { // if only holding ctrl (and or shift)
        if (toupper(translated_scancode) >= '@' && toupper(translated_scancode) <= '_') { // if char between C0 values
            translated_scancode = toupper(translated_scancode) - '@'; // get C0 control char
            if (translated_scancode == 0) explicit_nullbyte = 1; // ^@
        } else if (translated_scancode == '?') translated_scancode = 0x7F;
        else {
            tty_write_to_tty("^", 1, GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE)); // wasn't a valid ctrl escape, printing the raw escape value
        }
    }
    if ((scancode & ~KEY_BASE_SCANCODE_MASK & ~(KEY_MOD_LSHIFT_MASK) & ~(KEY_MOD_RSHIFT_MASK)) == KEY_MOD_LCONTROL_MASK && translated_scancode == '?') translated_scancode = '\x7F'; // can't get to ? on english layout without shift
    if (translated_scancode != '\0' || explicit_nullbyte) tty_write_to_tty(&translated_scancode, 1, GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE));
}