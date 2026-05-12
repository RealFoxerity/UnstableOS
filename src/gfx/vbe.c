#include "gfx/vbe.h"
#include "gfx/vga.h"
#include "edid.h"

#include "v8086.h"
#include "lowlevel.h"
#include "kernel.h"
#include <string.h>

#include "endian.h"
#include "mm/kernel_memory.h"

// WARNING: this code is NOT interrupt safe
// printing in an ISR in the same context as a previously running operation WILL DEADLOCK on the framebuffer lock!

// tested in:
// (virtualized) VMWare, QXL, Cirrus
// (real) Intel 82945M, Intel X3100, Intel Iris XE (8th gen), NVidia GTX 1050-ti, and AMD Radeon RX 7800-XT
// all worked
struct VBEIBlock     * vbe_info_block = (void*)0xA000;
struct VBE_mode_info * vbe_mode_info  = (void*)0xA200;
struct EDID_block    * vbe_edid_block = (void*)0xA300;

const struct VBE_modes_list * vbe_current_mode = NULL;


size_t vbe_framebuffer_size = 0;

#define VBE_SIGNATURE "VESA"
#define VBE2_SIGNATURE "VBE2"
#define VBE_SUCCESS_AX 0x004F

#define VBE_CAP_DAC_8BIT 1 // can set the dac to 8 bits per channel/color, otherwise 6 bits fixed

#define X86_VIDEO_INT 0x10

#define VBE_GET_CONTROLLER_INFO 0x4F00
#define VBE_GET_MODE_INFO 0x4F01
#define VBE_SET_MODE_INFO 0x4F02

#define VBE_MODE_USE_LINEAR_FB 0x4000 // as opposed to a windowed fb

#define VBE_DDC 0x4F15

struct VBE_modes_list * vbe_modes_list = NULL;

void vbe_gather_info() {
    kassert(vbe_modes_list == NULL);
    memset(vbe_info_block, 0, sizeof(struct VBEIBlock));
    // notify the VESA bios that we support VBE 2
    memcpy(vbe_info_block->signature, VBE2_SIGNATURE, sizeof(VBE2_SIGNATURE) - 1);
    v86_mcontext_t vbe_returns;
    vbe_returns = v86_call_bios(X86_VIDEO_INT, (v86_mcontext_t){.eax = VBE_GET_CONTROLLER_INFO, .edi = (unsigned long)vbe_info_block});

    // having the VBE2 signature there gets us the 512 byte structure, but the signature always has to
    // be VESA at the end of the interrupt
    if (vbe_returns.eax != VBE_SUCCESS_AX || memcmp(vbe_info_block->signature, VBE_SIGNATURE , sizeof(VBE_SIGNATURE)  - 1) != 0) {
        kprintf("VBE not supported, will be sticking to VGA\n");
        return;
    }
    if (vbe_info_block->version < 0x200) {
        kprintf("Stub: we don't yet support VBE 1.x (no banked framebuffer support), will be sticking to VGA\n");
    }

    vbe_framebuffer_size = vbe_info_block->total_memory_64k_blocks * 64 * 1024;
    kprintf("VBE supported, v0x%hx, total memory %lu\n", vbe_info_block->version, vbe_framebuffer_size);

    if (vbe_framebuffer_size > LINEAR_FRAMEBUFFER_MAX_SIZE) {
        kprintf("VBE: Warning: Framebuffer size larger than %d MiB, will not map rest!\n",
            LINEAR_FRAMEBUFFER_MAX_SIZE/1024/1024);
        vbe_framebuffer_size = LINEAR_FRAMEBUFFER_MAX_SIZE;
    }

    char * vbe_oem_string     = V86_FAR2LIN(vbe_info_block->oem_string    [1], vbe_info_block->oem_string    [0]);
    char * vbe_vendor_string  = V86_FAR2LIN(vbe_info_block->vendor_string [1], vbe_info_block->vendor_string [0]);
    char * vbe_product_string = V86_FAR2LIN(vbe_info_block->product_string[1], vbe_info_block->product_string[0]);
    kprintf("OEM: %s, Vendor: %s, Product: %s\n", vbe_oem_string, vbe_vendor_string, vbe_product_string);

    unsigned short * video_modes = V86_FAR2LIN(vbe_info_block->video_modes[1], vbe_info_block->video_modes[0]);
    if (*video_modes == 0xFFFF) {
        kprintf("VBE: No available modes, ignoring device\n");
        return;
    }

    size_t loaded_modes = 0;
    for (; *video_modes != 0xFFFF; video_modes++) {
        memset(vbe_mode_info, 0, sizeof(struct VBE_mode_info));
        if (video_modes > (unsigned short*)0x100000) {
            kprintf("Warning: The VBE modes list overran the real mode 1MiB address space limit!\n");
            break;
        }

        vbe_returns = v86_call_bios(X86_VIDEO_INT,
            (v86_mcontext_t) {
                .eax = VBE_GET_MODE_INFO,
                .ecx = *video_modes,
                .edi = (unsigned long)vbe_mode_info
            });

        if (vbe_returns.eax != VBE_SUCCESS_AX) {
            kprintf("VBE: Failed to get video mode %hx, giving up!\n", *video_modes);
            return;
        }
        if (!vbe_mode_info->attributes.supported) {
            //kprintf("VBE: Advertised a mode that isn't supported (%hx)?\n", *video_modes);
            continue;
        }


        if (!vbe_mode_info->attributes.linear_framebuffer) continue; // i really don't want to fuck with planars again
        if ( vbe_mode_info->framebuffer_paddr == 0)        continue; // TODO: VirtualBox advertises linear FB with address 0?
        if (!vbe_mode_info->attributes.gfx_mode)           continue; // we don't support text
        if ( vbe_mode_info->bpp % 8 != 0 &&
             vbe_mode_info->bpp != 15)                     continue; // i refuse to do bit operations
        if ( vbe_mode_info->bpp > 32)                      continue; // we don't support better than 32 bits
        if ( vbe_mode_info->pitch * vbe_mode_info->height * vbe_mode_info->bpp / 8 > LINEAR_FRAMEBUFFER_MAX_SIZE)
            continue; // we wouldn't be able to map the framebuffer all in

        /*
        kprintf("VBE: (%hx) %ux%ux%hhu %c, fbaddr %lx\n",
            *video_modes,
            vbe_mode_info->width, vbe_mode_info->height,
            vbe_mode_info->bpp,
            vbe_mode_info->attributes.color_mode ? 'C':'M',
            vbe_mode_info->framebuffer_paddr
        );
        */

        switch (vbe_mode_info->memory_model) {
            case VBE_MEMORY_MODEL_PACKED:
            case VBE_MEMORY_MODEL_DIRECT:
            //case VBE_MEMORY_MODEL_YUV: // not yet supported by us
                goto ok;
            default: break;
        }
        continue;

        ok:
        struct VBE_modes_list * this = kalloc(sizeof(struct VBE_modes_list));
        kassert(this != NULL);
        this->mode_num = *video_modes;
        this->info = *vbe_mode_info;
        this->next = vbe_modes_list;
        vbe_modes_list = this;

        loaded_modes ++;
    }
    if (loaded_modes == 0) {
        kprintf("VBE: No eligible modes available, giving up!\n");
        return;
    }
    kprintf("VBE: Found %lu eligible modes\n", loaded_modes);

    gather_EDID_info_and_set_mode();
}

uint32_t * vbe_doubleframebuffer = NULL;

void vbe_set_info(const struct VBE_modes_list * mode) {
    v86_mcontext_t ret = v86_call_bios(X86_VIDEO_INT, (v86_mcontext_t){.eax = VBE_SET_MODE_INFO, .ebx = mode->mode_num | VBE_MODE_USE_LINEAR_FB});
    if (ret.eax != VBE_SUCCESS_AX) {
        kprintf("VBE: Warning: Failed to set mode %hx - error code %hx\n", mode->mode_num, (unsigned short)ret.eax);
        return;
    }
    // before mode setting in order for warnings from the realloc to go through with the old callbacks
    // for example VGA (that doesn't use the framebuffer) -> VBE, where reallocating printing a warning would page fault
    gfx_realloc_back_framebuffer(mode->info.width, mode->info.height);

    // this will momentarily break all colors :P
    // -> white = blue on mode 12
    vga_generate_256colors(); // we can reuse the VGA function for setting palettes for the 8 bit packed pixel

    vbe_current_mode = mode;
    current_video_funcs = &vbe_funcs;

    // ifs to avoid locking operations
    if (display_width > mode->info.width)
        display_width = mode->info.width;
    if (display_height > mode->info.height)
        display_height = mode->info.height;

    // has to be here instead of vbe info gather in case the mode has a different VRAM physical start address
    // using vbe_framebuffer_size instead of the specific one makes it easier for us to do thread safe clearing
    // among other things
    gfx_remap_framebuffer((void *)mode->info.framebuffer_paddr, vbe_framebuffer_size, 0);

    if (display_width < mode->info.width)
        display_width = mode->info.width;
    if (display_height < mode->info.height)
        display_height = mode->info.height;
}

static struct VBE_modes_list * vbe_get_specified_mode(int xres, int yres) {
    struct VBE_modes_list * best_mode = NULL;
    // try to get an exact mode
    for (struct VBE_modes_list * current = vbe_modes_list; current != NULL; current = current->next) {
        //kprintf("%u %u %p\n", current->info.width, current->info.height, current);
        if (current->info.width == xres && current->info.height == yres) {
            if (best_mode == NULL || current->info.bpp > best_mode->info.bpp)
                best_mode = current;
        }
    }
    if (best_mode != NULL) return best_mode;

    // try to get the closest mode available
    unsigned long requested_area = xres * yres;
    unsigned long best_delta   = -1;
    for (struct VBE_modes_list * current = vbe_modes_list; current != NULL; current = current->next) {
        unsigned long area = current->info.width * current->info.height;
        unsigned long area_delta = 0;
        if (area < requested_area)
            area_delta = requested_area - area;
        else
            area_delta = area - requested_area;

        if (best_mode == NULL || (area_delta == best_delta && current->info.bpp > best_mode->info.bpp)) {
            best_mode = current;
            best_delta = area_delta;
            continue;
        }
        if (area_delta < best_delta) {
            best_mode = current;
            best_delta = area_delta;
        }
    }
    return best_mode;
}

unsigned char vbe_set_mode(int xres, int yres) {
    struct VBE_modes_list * chosen_mode = vbe_get_specified_mode(xres, yres);
    if (chosen_mode == NULL) return 0;

    vbe_set_info(chosen_mode);
    return chosen_mode->info.bpp;
}

static struct VBE_modes_list * vbe_get_highest_mode() {
    struct VBE_modes_list * best_mode = vbe_modes_list;
    unsigned long area_best = best_mode->info.width * best_mode->info.height;

    for (struct VBE_modes_list * current = vbe_modes_list; current != NULL; current = current->next) {
        unsigned long area = current->info.width * current->info.height;
        if (area > area_best) {
            best_mode = current;
            area_best = area;
        }
    }
    //kprintf("%dx%d - %hx\n", best_mode->info.width, best_mode->info.height, best_mode->mode_num);
    return best_mode;
}

#define EDID_HEADER "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00"
static uint8_t check_edid_block(const struct EDID_block * edid_block) {
    if (memcmp(edid_block->header, EDID_HEADER, sizeof(EDID_HEADER) - 1) != 0)
        return 0;

    uint8_t checksum = 0;
    for (int i = 0; i < sizeof(struct EDID_block); i++) {
        checksum += ((unsigned char *)edid_block)[i];
    }
    return !checksum;
}

void gather_EDID_info_and_set_mode() {
    // get capabilities
    // TODO: ecx is monitor port, enumerate somehow?
    // ebx specifies level of DDC support (and blanking during EDID transfer)
    v86_mcontext_t vbe_returns = v86_call_bios(X86_VIDEO_INT, (v86_mcontext_t){.eax = VBE_DDC, .ebx = 0, .ecx = 0});
    if (vbe_returns.eax != VBE_SUCCESS_AX) {
#ifdef VBE_EDID_ASSUME_VIRTUAL_ON_FAILURE
        kprintf("VBE: Warning: Display doesn't support DDC, probably a virtual device, setting highest\n");
        vbe_set_info(vbe_get_highest_mode());
#else
        kprintf("VBE: Warning: Display doesn't support DDC, cannot determine correct resolution, setting safe\n");
        vbe_set_mode(640, 480); // one of the guaranteed once to be supported by each VESA device
#endif
        return;
    }

    vbe_returns = v86_call_bios(X86_VIDEO_INT, (v86_mcontext_t){
        .eax = VBE_DDC,
        .ebx = 1,
        .ecx = 0,
        .edx = 0,
        .edi = (unsigned long)vbe_edid_block
    });
    if (vbe_returns.eax != VBE_SUCCESS_AX ||
        (vbe_returns.edi & 0xFFFF) != (unsigned long)vbe_edid_block)
    {
        kprintf("VBE: Warning: Failed to read EDID, cannot determine correct resolution\n");
        return;
    }

    if (!check_edid_block(vbe_edid_block)) {
        kprintf("VBE: Warning: Returned EDID failed the checksum, cannot determine correct resolution\n");
        return;
    }

    struct VBE_modes_list * best_supported = vbe_get_specified_mode(640, 480); // guaranteed to exist
    kassert(best_supported);

    kprintf("Monitor EDID supported established timings:\n");
    if (vbe_edid_block->established_timings.vesa_720x400_70) {
        kprintf("\t720x400 @ 70 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(720, 400);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_720x400_88) {
        kprintf("\t720x400 @ 88 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(720, 400);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_640x480_60) {
        kprintf("\t640x480 @ 60 Hz\n");
    }
    if (vbe_edid_block->established_timings.vesa_640x480_67) {
        kprintf("\t640x480 @ 67 Hz\n");
    }
    if (vbe_edid_block->established_timings.vesa_640x480_72) {
        kprintf("\t640x480 @ 72 Hz\n");
    }
    if (vbe_edid_block->established_timings.vesa_640x480_75) {
        kprintf("\t640x480 @ 75 Hz\n");
    }
    if (vbe_edid_block->established_timings.vesa_800x600_56) {
        kprintf("\t800x600 @ 56 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(800, 600);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_800x600_60) {
        kprintf("\t800x600 @ 60 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(800, 600);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_800x600_72) {
        kprintf("\t800x600 @ 72 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(800, 600);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_800x600_75) {
        kprintf("\t800x600 @ 75 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(800, 600);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_832x624_75) {
        kprintf("\t832x624 @ 75 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(832, 624);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_1024x768i_87) {
        kprintf("\t1024x768 interlaced @ 87 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(1024, 728);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_1024x768_60) {
        kprintf("\t1024x768 @ 60 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(1024, 728);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_1024x768_70) {
        kprintf("\t1024x768 @ 70 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(1024, 728);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_1024x768_75) {
        kprintf("\t1024x768 @ 75 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(1024, 728);
        if (temp != NULL) best_supported = temp;
    }
    if (vbe_edid_block->established_timings.vesa_1280x1024_75) {
        kprintf("\t1280x1024 @ 75 Hz\n");
        struct VBE_modes_list * temp = vbe_get_specified_mode(1280, 1024);
        if (temp != NULL) best_supported = temp;
    }

    kprintf("Monitor EDID supported standard timings:\n");
    // there are 8 standard timings in the edid block specified
    for (int i = 0; i < 8; i++) {
        if (vbe_edid_block->_standard_timings[i] == 0x0101) break; // means that all further ones are unused

        int horizontal_res = (vbe_edid_block->standard_timings[i].horizontal_resolution + 31) * 8;
        int vertical_res = 0;
        switch (vbe_edid_block->standard_timings[i].aspect_ratio) {
            case EDID_MAN_AR_16_10:
                vertical_res = horizontal_res/16*10;
                break;
            case EDID_MAN_AR_4_3:
                vertical_res = horizontal_res/4*3;
                break;
            case EDID_MAN_AR_5_4:
                vertical_res = horizontal_res/5*4;
                break;
            case EDID_MAN_AR_16_9:
                vertical_res = horizontal_res/16*9;
                break;
        }
        int refresh_rate = vbe_edid_block->standard_timings[i].refresh_rate + 60;
        kprintf("\t%dx%d @ %d Hz\n", horizontal_res, vertical_res, refresh_rate);

        unsigned long best_area = best_supported->info.width * best_supported->info.height;
        unsigned long area = horizontal_res * vertical_res;
        if (area > best_area) {
            struct VBE_modes_list * temp = vbe_get_specified_mode(horizontal_res, vertical_res);
            if (temp != NULL) best_supported = temp;
        }
    }

    kprintf("Monitor EDID preferred timings:\n");
    char is_preferred = 0;
    for (int i = 0; i < 4; i++) {
        if (vbe_edid_block->data_blocks[i].preferred_timing.pixel_clock == 0) break; // not a detailed timing block
        int preferred_horizontal = vbe_edid_block->data_blocks[i].preferred_timing.horizontal_res_low | vbe_edid_block->
                                   data_blocks[i].preferred_timing.horizontal_res_high << 8;
        int preferred_vertical = vbe_edid_block->data_blocks[i].preferred_timing.vertical_res_low | vbe_edid_block->
                                 data_blocks[i].preferred_timing.vertical_res_high << 8;

        int preferred_hor_blank = vbe_edid_block->data_blocks[i].preferred_timing.horizontal_blank_low | vbe_edid_block->
                                  data_blocks[i].preferred_timing.
                                  horizontal_blank_high << 8;

        int preferred_hor_front_porch = vbe_edid_block->data_blocks[i].preferred_timing.horizontal_front_porch_low |
                                        vbe_edid_block->data_blocks[i].preferred_timing.horizontal_front_porch_high << 8;
        int preferred_hor_sync_width = vbe_edid_block->data_blocks[i].preferred_timing.horizontal_sync_pulse_width_low |
                                       vbe_edid_block->data_blocks[i].preferred_timing.horizontal_sync_pulse_width_high << 8;

        int preferred_complete_hor = preferred_horizontal + preferred_hor_blank + preferred_hor_front_porch +
                                     preferred_hor_sync_width;
        int preferred_refresh_rate = vbe_edid_block->data_blocks[i].preferred_timing.pixel_clock * 10 /
                                     preferred_complete_hor;
        kprintf("\t%dx%d @ %d Hz\n", preferred_horizontal, preferred_vertical, preferred_refresh_rate);
        unsigned long best_area = best_supported->info.width * best_supported->info.height;
        if (!is_preferred) best_area = 0;
        is_preferred = 1;
        unsigned long area = preferred_horizontal * preferred_vertical;
        if (area > best_area) {
            struct VBE_modes_list * temp = vbe_get_specified_mode(preferred_horizontal, preferred_vertical);
            if (temp != NULL) best_supported = temp;
        }
    }
    kprintf("Setting %dx%d %ubpp\n", best_supported->info.width, best_supported->info.height, best_supported->info.bpp);
    vbe_set_info(best_supported);
}


__attribute__((optimize("O3"))) static uint32_t vbe_get_direct_color(uint32_t color) {
    uint32_t direct_color = 0;
    direct_color =
            (color >> 24) >>
            (8 - vbe_current_mode->info.red_mask) <<
            (vbe_current_mode->info.red_position);
    direct_color |=
            (color >> 16 & 0xFF) >>
            (8 - vbe_current_mode->info.green_mask) <<
            (vbe_current_mode->info.green_position);
    direct_color |=
            (color >> 8 & 0xFF) >>
            (8 - vbe_current_mode->info.blue_mask) <<
            (vbe_current_mode->info.blue_position);
    return direct_color;
}

// these are unrolled on purpose to minimize conditionals!
__attribute__((optimize("O3"))) void vbe_swap_region(unsigned int start_x, unsigned int end_x, unsigned int start_y, unsigned int end_y) {
    if (back_framebuffer == NULL) return;
    spinlock_acquire_interruptible(&framebuffer_lock);
    if (start_x >= back_framebuffer_w || start_y >= back_framebuffer_h) {
        spinlock_release(&framebuffer_lock);
        return;
    }
    if (end_x >= back_framebuffer_w) end_x = back_framebuffer_w - 1;
    if (end_y >= back_framebuffer_h) end_y = back_framebuffer_h - 1;

    switch (vbe_current_mode->info.bpp) {
        case 32:
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    ((uint32_t *)LINEAR_FRAMEBUFFER_START)[y * vbe_current_mode->info.pitch / 4 + x] =
                        back_framebuffer[y * back_framebuffer_w + x];
                }
            }
            break;
        case 16:
        case 15:
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    ((uint16_t *)LINEAR_FRAMEBUFFER_START)[y * vbe_current_mode->info.pitch / 2 + x] =
                        back_framebuffer[y * back_framebuffer_w + x] & 0xFFFF;
                }
            }
            break;
        case 8:
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    ((uint8_t *)LINEAR_FRAMEBUFFER_START)[y * vbe_current_mode->info.pitch + x] =
                        back_framebuffer[y * back_framebuffer_w + x] & 0xFF;
                }
            }
        default:
            break;
    }
    spinlock_release(&framebuffer_lock);
}

void vbe_hw_shift_pixels(unsigned int pixels) {}

struct gfx_funcs vbe_funcs = {
    .clear_screen = vbe_clear,
    .swap_region = vbe_swap_region,
    .write_pixel_buffered = vbe_write_pixel_buffered,
    .fill_buffered = vbe_fill_buffered,
    .copy_region_unbuffered = vbe_copy_region_unbuffered,
    .read_framebuffer = vbe_read_framebuffer,
    .hw_shift_pixels = vbe_hw_shift_pixels,
    .hw_shift_scanlines = vbe_hw_shift_scanlines,
};

// TODO: implement converting to normal rgb
// thinking of moving to the way linux does things
// just don't solve this and leave it up to the userspace to parse colors
__attribute__((optimize("O3"))) uint32_t vbe_read_framebuffer(unsigned int x, unsigned int y) {
    if (y >= display_height) return 0;
    if (x >= display_width) return 0;

    spinlock_acquire_interruptible(&framebuffer_lock);
    if (back_framebuffer != NULL) {
        if (y < back_framebuffer_h &&
            x < back_framebuffer_w
        ) {
            uint32_t color = back_framebuffer[y * back_framebuffer_w + x];
            spinlock_release(&framebuffer_lock);
            return color;
        }
    }
    spinlock_release(&framebuffer_lock);

    // fall back to unbuffered VRAM read - very slow
    void * dest = LINEAR_FRAMEBUFFER_START + y * vbe_current_mode->info.pitch + x * vbe_current_mode->info.bpp/8;

    uint32_t read_color = 0;
    switch(vbe_current_mode->info.memory_model) {

        case VBE_MEMORY_MODEL_PACKED:
            return *(unsigned char*)dest;
        //case VBE_MEMORY_MODEL_YUV:
        case VBE_MEMORY_MODEL_DIRECT:
            switch (vbe_current_mode->info.bpp) {
                case 32:
                    read_color |= *(unsigned char*)(dest + 3) << 24;
                case 24:
                    read_color |= *(unsigned char*)(dest + 2) << 16;
                case 16:
                case 15:
                    read_color |= *(unsigned char*)(dest + 1) << 8;
                case 8:
                    read_color |= *(unsigned char*)(dest + 0) << 0;
                default: break;
            }
            return read_color;
        default:
            return *(unsigned char *)dest;
    }
}

void vbe_clear() {
    spinlock_acquire_interruptible(&framebuffer_lock);
    memset(LINEAR_FRAMEBUFFER_START, 0, vbe_framebuffer_size);
    if (back_framebuffer != NULL) {
        memset(back_framebuffer, 0, back_framebuffer_w * back_framebuffer_h * sizeof(*back_framebuffer));
    }
    spinlock_release(&framebuffer_lock);
}

static void __vbe_write_framebuffer_unbuffered(unsigned int x, unsigned int y, uint32_t raw);
void vbe_write_framebuffer_buffered(unsigned int x, unsigned int y, uint32_t raw) {
    spinlock_acquire_interruptible(&framebuffer_lock);
    if (back_framebuffer != NULL) {
        if (y < back_framebuffer_h &&
            x < back_framebuffer_w
        ) {
            back_framebuffer[y * back_framebuffer_w + x] = raw;
        }
    } else
        __vbe_write_framebuffer_unbuffered(x, y, raw);
    spinlock_release(&framebuffer_lock);
}
void vbe_write_pixel_buffered(unsigned int x, unsigned int y, uint32_t color, char use_palette) {
    if (y >= display_height) return;
    if (x >= display_width) return;

    if (use_palette) color = console_colors[color & 0xF];

    uint32_t direct_color = vbe_get_direct_color(color);
    uint32_t packed_color = VGA_RGB32_TO_RGB8(color);
    if (vbe_current_mode->info.memory_model == VBE_MEMORY_MODEL_PACKED) {
        vbe_write_framebuffer_buffered(x, y, packed_color);
    } else {
        vbe_write_framebuffer_buffered(x, y, direct_color);
    }
}
static void __vbe_write_framebuffer_unbuffered(unsigned int x, unsigned int y, uint32_t raw) {
    void * dest = LINEAR_FRAMEBUFFER_START + y * vbe_current_mode->info.pitch + x * vbe_current_mode->info.bpp/8;

    switch(vbe_current_mode->info.memory_model) {
        // 0x00 - 0x03 aren't used at all by us and aren't that common
        // 0 = text mode, deselected in our mode gathering
        // 1 = CGA color, not likely to show up in hires
        // 2 = hercules graphics, extremely unlikely on real hardware
        // 3 = planar, deselected in our mode gathering

        case VBE_MEMORY_MODEL_PACKED:
            *(uint8_t *)dest = raw;
            return;
        // 5 = non-chain 4, 256 color, deselected in our mode gathering
        //case VBE_MEMORY_MODEL_YUV:
        case VBE_MEMORY_MODEL_DIRECT:
            // <= VBE 1.1 (before 1991) didn't support channel masks
            // however because we chose to only support linear framebuffers
            // which came out with VBE 1.2, we don't have to support the
            // old bpp default pixel structures

            switch (vbe_current_mode->info.bpp) {
                case 8:
                    *(uint8_t *)dest = raw;
                    break;
                case 15:
                case 16:
                    *(uint16_t *)dest = raw;
                    break;
                case 24:
                    *(uint8_t *)dest = raw >> 16;
                    *(uint8_t *)dest = raw >> 8;
                    *(uint8_t *)dest = raw >> 0;
                    break;
                case 32:
                    *(uint32_t *)dest = raw;
                    break;
                default:
                    return;
            }
            break;
        // all further reserved and/or OEM
        default:
            return;
    }
}

static void __vbe_copy_region_buffered(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned final_x, unsigned int final_y) {
    if (final_y > y + height || final_y < y) {
        for (unsigned int i = 0; i < height; i++) {
            memmove(
                back_framebuffer + (final_y + i) * back_framebuffer_w + final_x,
                back_framebuffer + (y + i)       * back_framebuffer_w + x,
                width * sizeof(*back_framebuffer)
            );
        }
    } else {
        for (unsigned int i = height; i > 0; i--) {
            memmove(
                back_framebuffer + (final_y + i - 1) * back_framebuffer_w + final_x,
                back_framebuffer + (y + i - 1)       * back_framebuffer_w + x,
                width * sizeof(*back_framebuffer)
            );
        }
    }
}

static void __vbe_copy_region_unbuffered(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned final_x, unsigned int final_y) {
    if (final_y > y + height || final_y < y) {
        for (unsigned int i = 0; i < height; i++) {
            memmove(
                (void*)back_framebuffer + (final_y + i) * vbe_current_mode->info.pitch + final_x * vbe_current_mode->info.bpp/8,
                (void*)back_framebuffer + (final_y + i) * vbe_current_mode->info.pitch + x * vbe_current_mode->info.bpp/8,
                width * vbe_current_mode->info.bpp/8
            );
        }
    } else {
        for (unsigned int i = height; i > 0; i--) {
            memmove(
                (void*)back_framebuffer + (final_y + i - 1) * vbe_current_mode->info.pitch + final_x * vbe_current_mode->info.bpp/8,
                (void*)back_framebuffer + (final_y + i - 1) * vbe_current_mode->info.pitch + x * vbe_current_mode->info.bpp/8,
                width * vbe_current_mode->info.bpp/8
            );
        }
    }
}

void vbe_copy_region_unbuffered(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int final_x, unsigned int final_y) {
    if (final_x >= display_width || final_y >= display_height) return;
    if (final_x == x && final_y == y) return;

    if (x >= display_width)  x = display_width - 1;
    if (y >= display_height) y = display_height - 1;

    if (x + width > display_width) width = display_width - x;
    if (y + height > display_height) height = display_height - y;

    if (final_x + width > display_width) width = display_width - final_x;
    if (final_y + height > display_height) height = display_height - final_y;


    spinlock_acquire_interruptible(&framebuffer_lock);
    if (back_framebuffer != NULL &&
        display_width == back_framebuffer_w &&
        display_height == back_framebuffer_h // couldn't be bother checking everything again
    ) {
        __vbe_copy_region_buffered(x, y, width, height, final_x, final_y);
    } else {
        __vbe_copy_region_unbuffered(x, y, width, height, final_x, final_y);
    }
    spinlock_release(&framebuffer_lock);

    vbe_swap_region(final_x, final_x + width - 1, final_y, final_y + height - 1);
}

void vbe_fill_buffered(unsigned int start_x, unsigned int end_x, unsigned start_y, unsigned int end_y, uint32_t color, char use_palette) {
    if (start_x >= display_width)  start_x = display_width - 1;
    if (start_y >= display_height) start_y = display_height - 1;

    if (end_x >= display_width) end_x = display_width - 1;
    if (end_y >= display_height) end_y = display_height - 1;

    if (use_palette) color = console_colors[color & 0xF];

    uint32_t direct_color = vbe_get_direct_color(color);
    uint32_t packed_color = VGA_RGB32_TO_RGB8(color);

    spinlock_acquire_interruptible(&framebuffer_lock);

    if (back_framebuffer != NULL &&
        display_width == back_framebuffer_w &&
        display_height == back_framebuffer_h)
    {
        if (vbe_current_mode->info.memory_model == VBE_MEMORY_MODEL_PACKED) {
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    back_framebuffer[y * back_framebuffer_w + x] = packed_color;
                }
            }
        } else {
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    back_framebuffer[y * back_framebuffer_w + x] = direct_color;
                }
            }
        }
    } else {
        if (vbe_current_mode->info.memory_model == VBE_MEMORY_MODEL_PACKED) {
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    __vbe_write_framebuffer_unbuffered(x, y, packed_color);
                }
            }
        } else {
            for (unsigned int y = start_y; y <= end_y; y++) {
                for (unsigned int x = start_x; x <= end_x; x++) {
                    __vbe_write_framebuffer_unbuffered(x, y, direct_color);
                }
            }
        }
    }

    spinlock_release(&framebuffer_lock);
    if (back_framebuffer != NULL) vbe_swap_region(start_x, end_x, start_y, end_y);
}
void vbe_hw_shift_scanlines(unsigned int scanlines) {
    spinlock_acquire_interruptible(&framebuffer_lock);
    if (back_framebuffer != NULL) {
        for (unsigned int i = scanlines; i < back_framebuffer_h; i++) {
            memcpy(back_framebuffer + (i-scanlines) * back_framebuffer_w,
                    back_framebuffer + i * back_framebuffer_w,
                    back_framebuffer_w * sizeof(*back_framebuffer));
        }
    } else {
        for (unsigned int i = scanlines; i < display_height; i++) {
            memcpy(LINEAR_FRAMEBUFFER_START + (i-scanlines) * vbe_current_mode->info.pitch,
                    LINEAR_FRAMEBUFFER_START + i * vbe_current_mode->info.pitch,
                    vbe_current_mode->info.pitch);
        }
    }
    spinlock_release(&framebuffer_lock);

    vbe_swap_region(0, display_width, 0, display_height - scanlines);
}
