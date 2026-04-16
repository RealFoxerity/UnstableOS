#ifndef EDID_H
#define EDID_H

enum edid_analog_signal_leveling {
    EDID_A_SIG_LEVEL_0700_0300_1000,
    EDID_A_SIG_LEVEL_0714_0286_1000,
    EDID_A_SIG_LEVEL_1000_0400_1400,
    EDID_A_SIG_LEVEL_0700_0000_0700,
};

// per primary color / channel
enum edid_digital_color_bit_depth {
    EDID_D_COLOR_DEPTH_UNDEFINED,
    EDID_D_COLOR_DEPTH_6,
    EDID_D_COLOR_DEPTH_8,
    EDID_D_COLOR_DEPTH_10,
    EDID_D_COLOR_DEPTH_12,
    EDID_D_COLOR_DEPTH_14,
    EDID_D_COLOR_DEPTH_16,
    EDID_D_COLOR_DEPTH_RESV
};

enum edid_digital_video_interfaces {
    EDID_D_INTERFACE_UNDEFINED,
    EDID_D_INTERFACE_DVI,
    EDID_D_INTERFACE_HDMI_A,
    EDID_D_INTERFACE_HDMI_B,
    EDID_D_INTERFACE_MDDI,
    EDID_D_INTERFACE_DISPLAYPORT,
    // rest are reserved
};

enum edid_analog_color_type {
    EDID_A_COLOR_MONOCHROME,
    EDID_A_COLOR_RGB,
    EDID_A_COLOR_NONRGB,
    EDID_A_COLOR_UNDEFINED
};

enum edid_digital_color_encoding {
    EDID_D_COLOR_RGB_4_4_4,
    EDID_D_COLOR_RGB_YCRCB_4_4_4,
    EDID_D_COLOR_RGB_4_4_4_YCRCB_4_4_2,
    EDID_D_COLOR_RGB_YCRCB_4_4_4_YCRCB_4_4_2
};


enum edid_manufacturer_ars {
    EDID_MAN_AR_16_10,
    EDID_MAN_AR_4_3,
    EDID_MAN_AR_5_4,
    EDID_MAN_AR_16_9
};

struct EDID_block {
    unsigned char header[8]; // 00 FF FF FF FF FF FF 00
    struct {
        uint16_t manufacturer; // compressed (5 bit) ascii
        uint8_t product[2];
        uint32_t serial;
        uint8_t manufacture_week; // 1 - 54, or 0xFF
        uint8_t manufacture_year; // if week = 0xFF, then year of the model, otherwise year of manufacture
    } __attribute__((packed)) product_info;
    struct {
        uint8_t version;
        uint8_t revision;
    } __attribute__((packed)) version;
    struct {
        union {
            struct {
                uint8_t supported_serration_on_vsync          : 1;
                uint8_t supported_composite_sync_on_green     : 1;
                uint8_t supported_composite_sync_on_hor       : 1;
                uint8_t supported_separate_h_v_sync           : 1;
                uint8_t blank_to_black_setup                  : 1;
                enum edid_analog_signal_leveling signal_level : 2;
                uint8_t is_digital                            : 1;
            } __attribute__((packed)) analog_input_definition;

            struct {
                enum edid_digital_video_interfaces supported_interface : 4;
                enum edid_digital_color_bit_depth color_bit_depth      : 3;
                uint8_t is_digital                                     : 1;
            } __attribute__((packed)) digital_input_definition;
        };

        // if either is 0, then the other is aspect ratio
        // if both 0, undefined
        uint8_t horizontal_size; // 1 - 254 cm, or landscape AR 1 : 1 - 3.54 : 1
        uint8_t vertical_size; // 1 - 254 cm, or portrait AR 1 : 1 - 1 : 3.54 ~ 0.28 : 1 - 0.99 : 1

        uint8_t gamma_factor; // 1 - 3.54, if FF then not defined and in DI-EXT
        union {
            struct {
                uint8_t continuous_frequency       : 1; // if set, supports both CVT and GTF, otherwise only CVT
                uint8_t preferred_timing_is_native : 1; // native pixel format and preferred refresh rate
                uint8_t srgb_is_default            : 1;

                enum edid_analog_color_type supported_color_types : 2;

                uint8_t supported_active_off_is_low_power : 1;
                uint8_t supported_suspend                 : 1;
                uint8_t supported_standby                 : 1;
            } __attribute__((packed)) analog_features;
            struct {
                uint8_t continuous_frequency       : 1; // if set, supports both CVT and GTF, otherwise only CVT
                uint8_t preferred_timing_is_native : 1; // native pixel format and preferred refresh rate
                uint8_t srgb_is_default            : 1;

                enum edid_digital_color_encoding supported_color_encoding : 2;

                uint8_t supported_active_off_is_low_power : 1;
                uint8_t supported_suspend                 : 1;
                uint8_t supported_standby                 : 1;
            } __attribute__((packed)) digital_features;
        };
    } __attribute__((packed)) basic_parameters;
    struct {
        uint8_t rg_low; // rx1, rx0, ry1, ry0, gx1, gx0, gy1, gy0
        uint8_t bw_low;
        uint8_t red_x_high;
        uint8_t red_y_high;
        uint8_t green_x_high;
        uint8_t green_y_high;
        uint8_t blue_x_high;
        uint8_t blue_y_high;
        uint8_t white_x_high;
        uint8_t white_y_high;
    } __attribute__((packed)) color_characteristics;
    struct {
        struct {
            // timings 1
            uint8_t vesa_800x600_60 : 1;
            uint8_t vesa_800x600_56 : 1;
            uint8_t vesa_640x480_75 : 1;
            uint8_t vesa_640x480_72 : 1;
            uint8_t vesa_640x480_67 : 1;
            uint8_t vesa_640x480_60 : 1;
            uint8_t vesa_720x400_88 : 1;
            uint8_t vesa_720x400_70 : 1;
        };

        // timings 2
        struct {
            uint8_t vesa_1280x1024_75 : 1;
            uint8_t vesa_1024x768_75  : 1;
            uint8_t vesa_1024x768_70  : 1;
            uint8_t vesa_1024x768_60  : 1;
            uint8_t vesa_1024x768i_87 : 1;
            uint8_t vesa_832x624_75   : 1;
            uint8_t vesa_800x600_75   : 1;
            uint8_t vesa_800x600_72   : 1;
        };
        uint8_t manufacturer_timings;
    } __attribute__((packed)) established_timings;

    union {
        uint16_t _standard_timings[8];
        struct {
            uint8_t horizontal_resolution; // = (actual / 8) - 31
            uint8_t refresh_rate : 6; // actual - 60
            enum edid_manufacturer_ars aspect_ratio : 2;
        } __attribute__((packed)) standard_timings[8]; // 0x01 means unused
    };

    union {
        struct {
            uint16_t pixel_clock; // actual / 10'000 -> clock in 10 KHz steps from 10 KHz to 655.35 MHz, 0 is reserved

            uint8_t horizontal_res_low;
            uint8_t horizontal_blank_low;
            uint8_t horizontal_blank_high : 4;
            uint8_t horizontal_res_high   : 4;

            uint8_t vertical_res_low;
            uint8_t vertical_blank_low;
            uint8_t vertical_blank_high : 4;
            uint8_t vertical_res_high   : 4;

            uint8_t horizontal_front_porch_low;
            uint8_t horizontal_sync_pulse_width_low;
            uint8_t vertical_sync_pulse_width_low : 4;
            uint8_t vertical_front_porch_low      : 4;

            uint8_t vertical_sync_pulse_width_high   : 2;
            uint8_t vertical_front_porch_high        : 2;
            uint8_t horizontal_sync_pulse_width_high : 2;
            uint8_t horizontal_front_porch_high      : 2;

            uint8_t horizontal_res_mm_low;
            uint8_t vertical_res_mm_low;
            uint8_t vertical_res_mm_high   : 4;
            uint8_t horizontal_res_mm_high : 4;

            uint8_t hor_border_pixels;
            uint8_t ver_border_pixels;

            uint8_t details; // TODO: some struct for this? doesn't seem useful anyway...
        } __attribute__((packed)) preferred_timing;
        // TODO: implement all data blocks :P
    } data_blocks[4];
    uint8_t extension_count;
    uint8_t checksum; // sum of all 128 should be 0
} __attribute__((packed));

// in vbe.c
void gather_EDID_info_and_set_mode();
#endif