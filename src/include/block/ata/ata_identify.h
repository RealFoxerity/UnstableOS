#ifndef _BLOCK_ATA_ATA_IDENTIFY_H
#define _BLOCK_ATA_ATA_IDENTIFY_H

#include <stdint.h>
#define ATA_IDENTIFY_BLOCK_SIZE 512

struct ata_identify {
    union {
        struct {
            struct {
                uint16_t                                    : 1; // obsolete since ATA-2
                uint16_t hard_sectored                      : 1; // obsolete since ATA-2
                uint16_t soft_sectored                      : 1; // obsolete since ATA-2, response incomplete since ATA-5
                uint16_t not_mfm                            : 1; // obsolete since ATA-2
                uint16_t head_switch_time_gt_15us           : 1; // obsolete since ATA-2
                uint16_t spindle_motor_control_impl         : 1; // obsolete since ATA-2
                uint16_t fixed_drive                        : 1; // not removable device
                uint16_t removable_cartridge_drive          : 1; // removable media
                uint16_t transfer_rate_lt_5mbps             : 1; // obsolete since ATA-2
                uint16_t transfer_rate_lt_10mbps            : 1; // obsolete since ATA-2
                uint16_t transfer_rate_gt_10mbps            : 1; // obsolete since ATA-2
                uint16_t rot_speed_tolerance_gt_0_5_percent : 1; // obsolete since ATA-2
                uint16_t data_strobe_offset_avail           : 1; // obsolete since ATA-2
                uint16_t track_offset_avail                 : 1; // obsolete since ATA-2
                uint16_t format_speed_gap_required          : 1; // obsolete since ATA-2
                uint16_t                                    : 1; // reserved for non-magnetic drives
            } general_config;
            uint16_t cylinder_count;
            uint16_t : 16; // reserved, specific configuration (?) since ATA-5
            uint16_t head_count;        // obsolete since ATA-6
            uint16_t bytes_per_track;   // obsolete since ATA-2
            uint16_t bytes_per_sector;  // obsolete since ATA-2
            uint16_t sectors_per_track; // obsolete since ATA-5
            uint16_t : 16; // vendor unique, reserved for CF Association since ATA-5
            uint16_t : 16; // vendor unique, reserved for CF Association since ATA-5
            uint16_t : 16; // vendor unique
            char serial_number[20];
            uint16_t buffer_type;      // obsolete since ATA-2
            uint16_t buffer_size_512b; // obsolete since ATA-2
            uint16_t ecc_bytes_rw_long_available; // obsolete since ATA-5
            char firmware_revision[8];
            char model_number[40];
            uint16_t rw_multiple_max_sectors : 8; // per an IRQ
            uint16_t : 8; // vendor unique, 0x80 since ATA-6
            uint16_t doubleword_io_supported; // obsolete since ATA-2
            struct {
                uint16_t                    : 8; // vendor unique
                uint16_t dma_supported      : 1;
                uint16_t lba_supported      : 1;
                uint16_t iordy_can_disable  : 1; // since ATA-2
                uint16_t iordy_supported    : 1; // since ATA-2
                uint16_t __adv_pio_reserved : 1; // since ATA-2
                uint16_t standard_standby   : 1; // since ATA-2
                uint16_t : 2; // reserved
            } capabilities;
            uint16_t : 16; // reserved, Ob0100000000000000 | 1 if device specific standby minimum since ATA-5
            uint16_t : 8; // vendor unique, obsolete since ATA-5
            uint16_t pio_data_timing_mode : 8; // obsolete since ATA-5
            uint16_t : 8; // vendor unique, obsolete since ATA-5
            uint16_t dma_data_timing_mode  : 8; // obsolete since ATA-5
            uint16_t data_word_54_58_valid : 1;
            uint16_t data_word_64_70_valid : 1; // since ATA-2
            uint16_t data_word_88_valid    : 1; // since ATA-5
            uint16_t : 13; // reserved
            uint16_t current_cylinder_count;    // obsolete since ATA-6
            uint16_t current_head_count;        // obsolete since ATA-6
            uint16_t current_sectors_per_track; // obsolete since ATA-6
            uint32_t current_sector_capacity;   // obsolete since ATA-6
            uint16_t current_sectors_per_rw_multiple  : 8;
            uint16_t multiple_sector_setting_valid    : 1;
            uint16_t : 7; // reserved
            uint32_t total_user_lba;
            uint16_t singleword_dma_modes_supported   : 8; // obsolete since ATA-5
            uint16_t singleword_dma_mode_active       : 8; // obsolete since ATA-5
            uint16_t multipleword_dma_modes_supported : 8;
            uint16_t multipleword_dma_mode_active     : 8;

            // since ATA-2
            uint16_t adv_pio_modes_supported : 8;
            uint16_t : 8; // reserved
            uint16_t minimum_multiword_dma_word_cycle_nsec;
            uint16_t recommended_multiword_dma_cycle_nsec;
            uint16_t minimum_pio_transfer_no_flow_control_nsec;
            uint16_t minimum_pio_transfer_iordy_flow_control_nsec;
            uint32_t __adv_pio_reserved;

            // since ATA-5
            uint32_t __identify_packet_dev_resv0;
            uint32_t __identify_packet_dev_resv1;
            uint16_t maximum_queue_depth : 5; // -1
            uint16_t : 10; // reserved
            uint32_t : 32; // reserved, resv for SATA since ATA-8
            uint32_t : 32; // reserved, resv for SATA since ATA-8
            union {
                struct {
                    uint16_t : 1; // reserved
                    uint16_t : 1; // obsolete
                    uint16_t ata2 : 1; // obsolete since ATA-8
                    uint16_t ata3 : 1; // obsolete since ATA-8
                    uint16_t ata4 : 1;
                    uint16_t ata5 : 1;
                    uint16_t ata6 : 1; // since ATA-8
                    uint16_t ata7 : 1; // since ATA-8
                    uint16_t ata8 : 1; // since ATA-8
                    uint16_t __resv_for_ata9  : 1;
                    uint16_t __resv_for_ata10 : 1;
                    uint16_t __resv_for_ata11 : 1;
                    uint16_t __resv_for_ata12 : 1;
                    uint16_t __resv_for_ata13 : 1;
                    uint16_t __resv_for_ata14 : 1;
                    uint16_t : 1; // reserved
                } supported_major_versions;
                uint16_t __supported_major_versions;
            };
            uint16_t minor_version;
            struct {
                uint16_t smart               : 1;
                uint16_t security_mode       : 1;
                uint16_t removable_media     : 1;
                uint16_t power_management    : 1;
                uint16_t packet_command      : 1;
                uint16_t write_cache         : 1;
                uint16_t look_ahead          : 1;
                uint16_t release_interrupt   : 1;
                uint16_t service_interrupt   : 1;
                uint16_t device_reset        : 1;
                uint16_t host_protected_area : 1;
                uint16_t                     : 1; // obsolete
                uint16_t write_buffer        : 1;
                uint16_t read_buffer         : 1;
                uint16_t nop                 : 1;
                uint16_t                     : 1; // obsolete
            } command_set_supported;
            struct {
                uint16_t download_microcode            : 1;
                uint16_t rw_dma_queued                 : 1;
                uint16_t cfa                           : 1;
                uint16_t adv_power_management          : 1;
                uint16_t removable_media_status_notif  : 1;
                uint16_t power_up_in_standby           : 1;
                uint16_t set_features                  : 1;
                uint16_t                               : 1; // reserved for project 1407DT?
                uint16_t set_max_security_extension    : 1;
                uint16_t automatic_acoustic_management : 1; // since ATA-6
                uint16_t lba48                         : 1; // since ATA-6
                uint16_t device_config_overlay         : 1; // since ATA-6
                uint16_t flush_cache                   : 1; // since ATA-6
                uint16_t flush_cache_ext               : 1; // since ATA-6
                uint16_t                               : 1; // always 1
                uint16_t                               : 1; // always 0
            } command_set_supported2;
            struct {
                uint16_t smart_error_logging        : 1; // since ATA-6
                uint16_t smart_self_test            : 1; // since ATA-6
                uint16_t media_serial_number        : 1; // since ATA-6
                uint16_t media_card_pass_through    : 1; // since ATA-6
                uint16_t streaming                  : 1; // since ATA-6
                uint16_t general_purpose_logging    : 1; // since ATA-8
                uint16_t write_dma_fua_ext          : 1; // since ATA-8
                uint16_t write_dma_queued_fua_ext   : 1; // since ATA-8
                uint16_t wwn64                      : 1; // since ATA-8
                uint16_t                            : 2; // obsolete
                uint16_t                            : 2; // reserved for technical report TR-37-2004
                uint16_t idle_immediate_with_unload : 1;
                uint16_t                            : 1; // always 1
                uint16_t                            : 1; // always 0
            } command_set_supported3;
            struct {
                uint16_t smart               : 1;
                uint16_t security_mode       : 1;
                uint16_t removable_media     : 1;
                uint16_t power_management    : 1;
                uint16_t packet_command      : 1;
                uint16_t write_cache         : 1;
                uint16_t look_ahead          : 1;
                uint16_t release_interrupt   : 1;
                uint16_t service_interrupt   : 1;
                uint16_t device_reset        : 1;
                uint16_t host_protected_area : 1;
                uint16_t                     : 1; // obsolete
                uint16_t write_buffer        : 1;
                uint16_t read_buffer         : 1;
                uint16_t nop                 : 1;
                uint16_t                     : 1; // obsolete
            } command_set_enabled;
            struct {
                uint16_t download_microcode            : 1;
                uint16_t rw_dma_queued                 : 1;
                uint16_t cfa                           : 1;
                uint16_t adv_power_management          : 1;
                uint16_t removable_media_status_notif  : 1;
                uint16_t power_up_in_standby           : 1;
                uint16_t set_features                  : 1;
                uint16_t                               : 1; // reserved for project 1407DT?
                uint16_t set_max_security_extension    : 1;
                uint16_t automatic_acoustic_management : 1; // since ATA-6
                uint16_t lba48                         : 1; // since ATA-6
                uint16_t device_config_overlay         : 1; // since ATA-6
                uint16_t flush_cache                   : 1; // since ATA-6
                uint16_t flush_cache_ext               : 1; // since ATA-6
                uint16_t                               : 1; // always 1
                uint16_t                               : 1; // always 0
            } command_set_enabled2;
            struct {
                uint16_t smart_error_logging        : 1; // since ATA-6
                uint16_t smart_self_test            : 1; // since ATA-6
                uint16_t media_serial_number        : 1; // since ATA-6
                uint16_t media_card_pass_through    : 1; // since ATA-6
                uint16_t streaming                  : 1; // since ATA-6
                uint16_t general_purpose_logging    : 1; // since ATA-8
                uint16_t write_dma_fua_ext          : 1; // since ATA-8
                uint16_t write_dma_queued_fua_ext   : 1; // since ATA-8
                uint16_t wwn64                      : 1; // since ATA-8
                uint16_t                            : 2; // obsolete
                uint16_t                            : 2; // reserved for technical report TR-37-2004
                uint16_t idle_immediate_with_unload : 1;
                uint16_t                            : 1; // always 1
                uint16_t                            : 1; // always 0
            } command_set_enabled3;

            uint16_t ultra_dma_modes_supported : 8;
            uint16_t ultra_dma_mode_active : 8;
            uint16_t security_erase_unit_time_required;
            uint16_t security_enhanced_erase_unit_time_required;
            uint16_t current_adv_power_management_value;
            uint16_t master_password_revision_code;

            struct {
                // device 0 results, device 1 clears to 0
                uint16_t                                        : 1; // always 1
                uint16_t device0_number_determination           : 2; // 1 = jumper, 2 = CSEL signal, 3 = unknown
                uint16_t device0_passed_diag                    : 1;
                uint16_t device0_detected_pdiag_n               : 1;
                uint16_t device0_detected_dasp_n                : 1;
                uint16_t device0_responds_when_device1_selected : 1;
                uint16_t                                        : 1; // reserved

                // device 1 results, device 0 clears to 0
                uint16_t                              : 1; // always 1;
                uint16_t device1_number_determination : 2; // 1 = jumper, 2 = CSEL signal, 3 = unknown
                uint16_t device1_detected_pdiag_n     : 1;
                uint16_t                              : 1; // reserved

                uint16_t device_detected_cblid_n_above_Vih : 1; // else below Vil
                uint16_t                                   : 1; // always 1
                uint16_t                                   : 1; // always 0
            } hardware_reset_result;

            uint16_t current_acoustic_mgmt_value    : 8;
            uint16_t vendor_rec_acoustic_mgmt_value : 8;

            uint16_t stream_minimum_request_size;    // since ATA-6
            uint16_t stream_transfer_time;           // since ATA-6
            uint16_t stream_access_latency;          // since ATA-6
            uint32_t stream_performance_granularity; // since ATA-6
            uint64_t total_user_lba48;               // since ATA-6

            // and a bunch more stuff........
        } __attribute__((packed));
        uint16_t raw[256];
    };
};

// calls IDENTIFY DEVICE, sets drive properties accordingly, and returns a copy of the IDENTIFY struct
struct ata_identify * ata_identify(unsigned char bus_id, unsigned char drive_number);

#endif