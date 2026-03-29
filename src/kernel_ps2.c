#include "kernel_interrupts.h"
#include "kernel_sched.h"
#include "kernel_spinlock.h"
#include "keyboard.h"
#include "ps2_controller.h"
#include "kernel_ps2_lut.h"
#include "lowlevel.h"
#include "kernel.h"
#include <stdint.h>

#include <string.h>

#define kprintf(x, ...) kprintf("PS/2 driver: "x, ##__VA_ARGS__)

struct ps2_device {
    char present;
    char is_mouse;
    uint8_t type;
    char scan_code_set;
};

static struct ps2_device ps2_present_devices[2] = {0};


enum driver_states {
    PS2_UNINITIALIZED,
    PS2_INITIALIZED,
    PS2_DRIVER_RUNNING,
};
enum driver_states ps2_driver_state = PS2_UNINITIALIZED;

#define PS2_ID_WAIT_AMOUNT 3
#define PS2_RETRY_COUNT 3

static inline char ps2_output_buffer_full(uint8_t device_num) { // there is data to read from device/ctrl
    uint8_t status_reg = inb(PS2_COMM_PORT);
    return (status_reg & (device_num == 1 ? PS2_STATUS_OUTPUT_BUFFER_FULL : PS2_STATUS_DEVICE_2_OBUFFER_FULL)) != 0;
}

static inline char ps2_input_buffer_full() { // clear for writing
    uint8_t status_reg = inb(PS2_COMM_PORT);
    return (status_reg & PS2_STATUS_INPUT_BUFFER_FULL) != 0;
}

static inline void ps2_wait_until_ready_w() { // ready to write
    while (ps2_input_buffer_full());
}

#define PS2_BUFFER_WAIT_TIME 100
static inline void ps2_wait_until_ready_r(uint8_t device_num) { // ready to read
    // sometimes the mouse doesn't respond before initialization
    // and the PS2_STATUS_DEVICE_2_OBUFFER_FULL bit is implementation dependant anyway
    for (int i = 0; i < PS2_BUFFER_WAIT_TIME; i++)
        if (ps2_output_buffer_full(device_num)) return;
    // try the normal one
    for (int i = 0; i < PS2_BUFFER_WAIT_TIME; i++)
        if (ps2_output_buffer_full(1)) return;
    // just give up and assume broken wait, 200 io reads should be always enough anyway
}

static inline void ps2_prepare_send_port_2() {
    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_WRITE_PORT2_IN_BUF);
    ps2_wait_until_ready_w();
}


void kernel_reset_system() {
    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_WRITE_OUTPUT_PORT);
    ps2_wait_until_ready_w();
    outb(PS2_DATA_PORT, ~PS2_CONTROLLER_OUTPUTP_DONT_SYSTEM_RESET);
}



static inline char ps2_disable_scanning(char device_num) {
    uint8_t res_byte;
    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) {
            kprintf("Error: PS/2 device %d refused to disable scanning\n", device_num);
            return 0;
        }
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMAND_DISABLE_SCANNING);
        ps2_wait_until_ready_r(device_num);
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to disable scan command, attempt %d/3\n", device_num, i+1);
        }
    }
    return 1;
}
static inline char ps2_enable_scanning(char device_num) {
    uint8_t res_byte;
    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT){
            kprintf("Error: PS/2 device %d refused to enable scanning\n", device_num);
            return 0;
        }
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMAND_ENABLE_SCANNING);
        ps2_wait_until_ready_r(device_num);
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to enable scanning command, attempt %d/3\n", device_num, i+1);
        }
    }
    return 1;
}

static inline char test_ps2_device(char device_num) {
    if (!ps2_disable_scanning(device_num)) return 0;
    outb(PS2_COMM_PORT, device_num == 1?PS2_CONTROLLER_COMMAND_TEST_PORT1:PS2_CONTROLLER_COMMAND_TEST_PORT2);
    ps2_wait_until_ready_r(device_num);
    enum ps2_controller_port_test_response test_status = inb(PS2_DATA_PORT);

    switch (test_status) {
        case PS2_CONTROLLER_TEST_PASSED:
            kprintf("device %d passed port test, enabling\n", device_num);
            if (!ps2_enable_scanning(device_num)) return 0;
            return 1;
        case PS2_CONTROLLER_TEST_CLK_LINE_LOW:
            kprintf("Device %d failed port test: clock line stuck low\n", device_num);
            break;
        case PS2_CONTROLLER_TEST_CLK_LINE_HIGH:
            kprintf("Device %d failed port test: clock line stuck high\n", device_num);
            break;
        case PS2_CONTROLLER_TEST_DATA_LINE_LOW:
            kprintf("Device %d failed port test: clock data stuck low\n", device_num);
            break;
        case PS2_CONTROLLER_TEST_DATA_LINE_HIGH:
            kprintf("Device %d failed port test: clock data stuck high\n", device_num);
            break;
        default:
            kprintf("Device %d failed port test: Unknown error\n", device_num);
    }
    return 0;
}

static inline char ps2_keyboard_set_cs1(uint8_t device_num) {
#ifndef PS2_TRY_TO_NEGOTIATE_SC1
    return 0;
#else
    uint8_t res_byte;
    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) {
            errored:
            kprintf("Error: Device %d failed to switch to scan code set 1\n", device_num);
            return 0;
        }
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMAND_SCAN_CODE);
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_SCAN_CODE_SET_1);
        ps2_wait_until_ready_r(device_num);
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to set scancode set command, attempt %d/3\n", device_num, i+1);
        }
    }


    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) goto errored;
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMAND_SCAN_CODE);
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_SCAN_CODE_GET_CURRENT);
        ps2_wait_until_ready_r(device_num);
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to get scancode set command, attempt %d/3\n", device_num, i+1);
        }
    }
    ps2_wait_until_ready_r(device_num);
    res_byte = inb(PS2_DATA_PORT);
    if (res_byte != PS2_SCAN_CODE_SET_1) {
        kprintf("Error: PS/2 device %d refused to switch to scancode set 1 (got scs %hhx)\n", device_num, res_byte);
        return 0;
    }
    return 1;
#endif
}


static void ps2_errored_device(char device_num) {
    kprintf("Disabling device %d\n", device_num);
    outb(PS2_COMM_PORT, device_num == 1 ? PS2_CONTROLLER_COMMAND_DISABLE_PORT1:PS2_CONTROLLER_COMMAND_DISABLE_PORT2);
    ps2_present_devices[device_num - 1].present = 0;
}

static uint16_t ps2_get_id(uint8_t device_num) {
    uint8_t major = 0;
    for (int j = 0; j <= PS2_ID_WAIT_AMOUNT; j++) {
        if (j == PS2_ID_WAIT_AMOUNT) {
            kprintf("Unspecified PS/2 device on device number %d, assuming keyboard\n", device_num);
            return PS2_DEVICE_KEYBOARD;
        }
        if (ps2_output_buffer_full(device_num)) {
            major = inb(PS2_DATA_PORT);
            break;
        }
    }

    for (int j = 0; j <= PS2_ID_WAIT_AMOUNT; j++) {
        if (ps2_output_buffer_full(device_num)) {
            return (major << 8) | inb(PS2_DATA_PORT);
        }
    }
    return major << 8;
}

static char ps2_reset_mouse(uint8_t device_num) {
    uint8_t res_byte;
    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) {
            errored:
            ps2_errored_device(device_num);
            return 0;
        }
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMANDM_RESET);
        io_wait(); // mice don't follow the 0 bit of the status register
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 mouse %d responded with garbage to reset command, attempt %d/3\n", device_num, i+1);
        }
    }
    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) {
            kprintf("Warning: PS/2 mouse %d didn't come back after reset!\n", device_num);
            goto errored;
        }
        if (inb(PS2_DATA_PORT) == PS2_RESPONSE_SELF_TEST_PASSED) break;
    }
    return 1;
}

static char ps2_set_sample_rate(uint8_t device_num, uint8_t sample_rate) {
    uint8_t res_byte;
    switch (sample_rate) {
        case 10:
        case 20:
        case 40:
        case 80:
        case 100:
        case 200:
            for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
                if (i == PS2_RETRY_COUNT) {
                    ps2_errored_device(device_num);
                    return 0;
                }
                if (device_num == 2) ps2_prepare_send_port_2();
                ps2_wait_until_ready_w();
                outb(PS2_DATA_PORT, PS2_COMMANDM_SET_SAMPLE_RATE);
                io_wait(); // mice don't follow the 0 bit of the status register
                outb(PS2_DATA_PORT, sample_rate);
                io_wait();
                if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
                if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
                    kprintf("Warning: PS/2 mouse %d responded with garbage to set sample rate command, attempt %d/3\n", device_num, i+1);
                }
            }
            return 1;
        default: return 0;
    }
}

// will try to negotiate up to a 5 button mouse, returning the new type
static char ps2_mouse_switch_modes(uint8_t device_num) {
    if (!ps2_present_devices[device_num - 1].is_mouse) return 0;
    switch (ps2_present_devices[device_num - 1].type) {
        case PS2_DEVICE_STD_MOUSE:
            if (!ps2_set_sample_rate(device_num, 200)) return 0;
            if (!ps2_set_sample_rate(device_num, 100)) return 0;
            if (!ps2_set_sample_rate(device_num, 80)) return 0;
            if (ps2_get_id(device_num) >> 8 != PS2_DEVICE_MOUSE_SCROLL)
                return PS2_DEVICE_STD_MOUSE;
        case PS2_DEVICE_MOUSE_SCROLL:
            if (!ps2_set_sample_rate(device_num, 200)) return 0;
            if (!ps2_set_sample_rate(device_num, 200)) return 0;
            if (!ps2_set_sample_rate(device_num, 80)) return 0;
            if (ps2_get_id(device_num) >> 8 != PS2_DEVICE_5_BUTTON_MOUSE)
                return PS2_DEVICE_MOUSE_SCROLL;
        case PS2_DEVICE_5_BUTTON_MOUSE:
            return PS2_DEVICE_5_BUTTON_MOUSE;
        default: return 0;
    }
}

// TODO: the compaq bit to enable the aux port, didn't need it on my hw though
static char ps2_mouse_init(uint8_t device_num) {
    uint8_t res_byte;
    if (!ps2_reset_mouse(device_num)) return 0;
    ps2_present_devices[device_num - 1].type = ps2_mouse_switch_modes(device_num);
    if (!ps2_set_sample_rate(device_num, PS2_MOUSE_PACKET_SPEED)) return 0;
    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) {
            ps2_errored_device(device_num);
            return 0;
        }
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMANDM_ENABLE_PACKET_STREAMING);
        io_wait();
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) return 1;
        if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 mouse %d responded with garbage to enable streaming, attempt %d/3\n", device_num, i+1);
        }
    }
    return 1;
}

static inline void ps2_init_device(uint8_t device_num) { // todo: implement timer and checking bit 1 of status register
    uint8_t res_byte;
    if (!ps2_disable_scanning(device_num)) {
        errored:
        ps2_errored_device(device_num);
        return;
    }

    for (int i = 0; i <= PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) goto errored;
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMAND_ID_KEYBOARD);
        ps2_wait_until_ready_r(device_num);
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to id command, attempt %d/3\n", device_num, i+1);
        }
    }

    uint16_t device_ids = ps2_get_id(device_num);
    enum ps2_device_types id_major = device_ids >> 8;
    enum ps2_keyboard_types id_minor = device_ids & 0xff;
    char have_byte_2 = id_minor != 0;

    switch (id_major) {
        case PS2_DEVICE_STD_MOUSE:
            kprintf("Standard mouse on device number %d\n", device_num);
            ps2_present_devices[device_num - 1].is_mouse = 1;
            break;
        case PS2_DEVICE_MOUSE_SCROLL:
            kprintf("Mouse with scroll wheel on device number %d\n", device_num);
            ps2_present_devices[device_num - 1].is_mouse = 1;
            break;
        case PS2_DEVICE_5_BUTTON_MOUSE:
            kprintf("5 button mouse on device number %d\n", device_num);
            ps2_present_devices[device_num - 1].is_mouse = 1;
            break;

        case PS2_DEVICE_KEYBOARD:
        case PS2_DEVICE_NCD_SUN_KEYBOARD:
            if (!ps2_keyboard_set_cs1(device_num)) {
                kprintf("Falling back to scancode set 2\n");
                ps2_present_devices[device_num-1].scan_code_set = 2;
            } else ps2_present_devices[device_num-1].scan_code_set = 1;
            switch (id_minor) {
                case PS2_DEVICE_KEYBOARD_MF2_1:
                case PS2_DEVICE_KEYBOARD_MF2_2:
                case PS2_DEVICE_KEYBOARD_MF2_3:
                    kprintf("MF2 keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_SHORT:
                case PS2_DEVICE_SHORT_2:
                    kprintf("Short/Thinkpad keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_KEYBOARD_NCD_N97: // note: PS2_DEVICE_KEYBOARD_122_KEY_HCK not here because is the same value
                    kprintf("NCD N97 keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_KEYBOARD_122_KEY:
                    kprintf("122 key keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_KEYBOARD_JAPAN_G:
                    kprintf("Japanese G keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_KEYBOARD_JAPAN_P:
                    kprintf("Japanese P keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_KEYBOARD_JAPAN_A:
                    kprintf("Japanese A keyboard on device number %d\n", device_num);
                    break;
                case PS2_DEVICE_KEYBOARD_NCD_SUN_LAY:
                    kprintf("NCD (Sun layout) keyboard on device number %d\n", device_num);
                    break;
            }
            break;
        default:
            kprintf("Unknown device type on device number %d, assuming keyboard\n", device_num);
            if (!ps2_keyboard_set_cs1(device_num)) {
                kprintf("Falling back to scancode set 2\n");
                ps2_present_devices[device_num-1].scan_code_set = 2;
            } else ps2_present_devices[device_num-1].scan_code_set = 1;
            break;
    }

    ps2_present_devices[device_num - 1].present = 1;

    if (ps2_present_devices[device_num - 1].is_mouse) {
        ps2_mouse_init(device_num);
    } else {
        ps2_present_devices[device_num - 1].type = have_byte_2?id_minor : id_major;
    }
    if (!ps2_enable_scanning(device_num)) goto errored;
}

void ps2_init() {
    uint32_t prev_eflags = 0;
    asm volatile (
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(prev_eflags)
    );

    // implement check acpi

    if (ps2_output_buffer_full(1)) inb(PS2_DATA_PORT); // discard any remaining data

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_DISABLE_PORT1); // to stop them from sending interrupts
    io_wait();
    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_DISABLE_PORT2);

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_READ_BYTE);
    ps2_wait_until_ready_r(1);
    uint8_t config_byte = inb(PS2_DATA_PORT);
    config_byte |= //PS2_CONTROLLER_CONFIG_ENABLE_PORT1_TRANSLATION | // NOTE: translation doesn't do anything on port 2
                    //PS2_CONTROLLER_CONFIG_DISABLE_PORT1_CLOCK |    
                    //PS2_CONTROLLER_CONFIG_DISABLE_PORT2_CLOCK;
                    0;
    config_byte &= ~(PS2_CONTROLLER_CONFIG_ENABLE_PORT1_INTERRUPT | PS2_CONTROLLER_CONFIG_ENABLE_PORT2_INTERRUPT | PS2_CONTROLLER_CONFIG_ENABLE_PORT1_TRANSLATION);

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_WRITE_BYTE);
    ps2_wait_until_ready_w();
    outb(PS2_DATA_PORT, config_byte);

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_SELF_TEST);
    ps2_wait_until_ready_r(1);
    uint8_t self_test_status = inb(PS2_DATA_PORT);
    if (self_test_status != PS2_CONTROLLER_SELF_TEST_PASSED) {
        kprintf("WARNING: PS/2 CONTROLLER FAILED A SELF TEST\n");
        return;
    }

    
    
    uint8_t valid_devices = 0; // 1 | 2
    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_ENABLE_PORT1);
    if (test_ps2_device(1)) valid_devices |= 1;
    else outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_DISABLE_PORT1);


    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_ENABLE_PORT2); // if supported, config byte will have a 0 at PS2_CONTROLLER_CONFIG_DISABLE_PORT2_CLOCK
    io_wait();

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_READ_BYTE);
    ps2_wait_until_ready_r(1);
    config_byte = inb(PS2_DATA_PORT);

    if ((config_byte & PS2_CONTROLLER_CONFIG_DISABLE_PORT2_CLOCK) == 0) {
        // the controller has a second port (probably a mouse)
        if (test_ps2_device(2)) valid_devices |= 2;
        else outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_ENABLE_PORT2);
    }

    if (valid_devices == 0) {
        kprintf("WARNING: NO PS/2 DEVICES PASSED PORT TEST, GIVING UP\n");
        return;
    }

    if (valid_devices & 1) {
        ps2_init_device(1);
        config_byte |= PS2_CONTROLLER_CONFIG_ENABLE_PORT1_INTERRUPT;
        config_byte &= ~PS2_CONTROLLER_CONFIG_DISABLE_PORT1_CLOCK;
    }
    if (valid_devices & 2) {
        ps2_init_device(2);
        config_byte |= PS2_CONTROLLER_CONFIG_ENABLE_PORT2_INTERRUPT;
        config_byte &= ~PS2_CONTROLLER_CONFIG_DISABLE_PORT1_CLOCK;
    } else {
        outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_DISABLE_PORT2);
    }
    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_WRITE_BYTE);
    ps2_wait_until_ready_w();
    outb(PS2_DATA_PORT, config_byte);
    io_wait();

    ps2_driver_state = PS2_INITIALIZED;

    asm volatile ("push %0; popf;" ::"R"(prev_eflags));
}

enum ps2_internal_states {
    PS2_NORMAL,
    PS2_WAITING_FOR_ACK,
    PS2_WAITING_FOR_SECOND_BYTE,
    //PS2_WAITING_FOR_THIRD_BYTE,
    PS2_WAITING_FOR_PAUSE,
    PS2_WAITING_FOR_PAUSE_2, // the third byte
    PS2_WAITING_FOR_ECHO,


    PS2_SC2_WAITING_FOR_RELEASE,
    PS2_SC2_2B_WAITING_FOR_RELEASE,
    PS2_SC2_3B_WAITING_FOR_RELEASE, // still applies to pause
    PS2_SC2_WAITING_FOR_RELEASED_PAUSE, // different state because the release is set in the not always present second byte
    PS2_SC2_WAITING_FOR_RELEASED_PAUSE_2,

    PS2_MOUSE_NORMAL,
    PS2_MOUSE_WAITING_FOR_BYTE_2,
    PS2_MOUSE_WAITING_FOR_BYTE_3,
    PS2_MOUSE_WAITING_FOR_BYTE_4,
};

static enum ps2_internal_states internal_state_port1 = 0;
static enum ps2_internal_states internal_state_port2 = 0;


struct ps2_keyboard_state {
    uint16_t mods;
    uint8_t leds;
};

static struct ps2_keyboard_state keyboard_state = {0};


extern void console_translate_scancode(uint32_t scancode);


static void ps2_keyboard_driver_internal(char device_num) {
    if (!ps2_output_buffer_full(device_num)) {
        kprintf("Warning: Keyboard driver got called with data present for a different device, flushing\n");
        // we have to flush, otherwise the controller hangs :P
        inb(PS2_DATA_PORT);
        return;
    }
    uint32_t out = 0;

    uint8_t current_byte = inb(PS2_DATA_PORT);
    if (ps2_present_devices[device_num-1].present == 0) {
        kprintf("Recieved an interrupt for a disabled device! Ignoring\n");
        return;
    }
    enum ps2_internal_states * internal_state;
    if (device_num == 1) 
            internal_state = &internal_state_port1;
    else
            internal_state = &internal_state_port2;
    
    switch (*internal_state) {
        case PS2_NORMAL:
            if (ps2_present_devices[device_num-1].scan_code_set == 2 && current_byte == PS2_SC2_KEY_RELEASE_BYTE) {
                *internal_state = PS2_SC2_WAITING_FOR_RELEASE;
                break;
            }
            if (current_byte == PS2_2_BYTE_K_CODE) {
                *internal_state = PS2_WAITING_FOR_SECOND_BYTE;
                break;
            } else if (current_byte == PS2_3_BYTE_K_CODE) {
                *internal_state = PS2_WAITING_FOR_PAUSE;
                break;
            }
            if (ps2_present_devices[device_num-1].scan_code_set == 2) current_byte = ps2_sc2_to_1_lookup[current_byte];
            resolve_key:
            switch (current_byte & (~PS2_SC1_KEY_RELEASED_MASK)) {
                case KEY_LSHIFT:
                    keyboard_state.mods ^= PS2_KEY_MOD_LSHIFT_MASK;
                    break;
                case KEY_RSHIFT:
                    keyboard_state.mods ^= PS2_KEY_MOD_RSHIFT_MASK;
                    break;
                case KEY_LCONTROL:
                    keyboard_state.mods ^= PS2_KEY_MOD_LCONTROL_MASK;
                    break;
                case KEY_LALT:
                    keyboard_state.mods ^= PS2_KEY_MOD_LALT_MASK;
                    break;
                default: break;
            }
            switch (current_byte) { // we don't care about release
                case KEY_CAPSLOCK:
                    keyboard_state.leds ^= PS2_LED_CAPSLOCK;
                    keyboard_state.mods ^= PS2_KEY_MOD_CAPSLOCK_MASK;
                    break;
                case KEY_NUMLOCK:
                    keyboard_state.leds ^= PS2_LED_NUMBERLOCK;
                    keyboard_state.mods ^= PS2_KEY_MOD_NUMLOCK_MASK;
                    break;
                case KEY_SCROLLLOCK:
                    keyboard_state.leds ^= PS2_LED_SCROLLLOCK;
                    break;
                default: break;
            }
            out = (current_byte & (~PS2_SC1_KEY_RELEASED_MASK)) | ((current_byte&PS2_SC1_KEY_RELEASED_MASK)?KEY_RELEASED_MASK:0);
            out |= keyboard_state.mods << 12;
            break;
        case PS2_WAITING_FOR_SECOND_BYTE: 
            if (ps2_present_devices[device_num-1].scan_code_set == 2 && current_byte == PS2_SC2_KEY_RELEASE_BYTE) {
                *internal_state = PS2_SC2_2B_WAITING_FOR_RELEASE;
                break;
            }
            if (ps2_present_devices[device_num-1].scan_code_set == 2) current_byte = ps2_sc2_2byte_to_1_lookup[current_byte];
            resolve_2b_key:
            switch (current_byte & (~PS2_SC1_KEY_RELEASED_MASK)) {
                case PS2_SC1_B2_KEY_RALT:
                    keyboard_state.mods ^= PS2_KEY_MOD_RALT_MASK;
                    break;
                case PS2_SC1_B2_KEY_RCONTROL:
                    keyboard_state.mods ^= PS2_KEY_MOD_RCONTROL_MASK;
                    break;
                case PS2_SC1_B2_KEY_LMETA:
                    keyboard_state.mods ^= PS2_KEY_MOD_LMETA_MASK;
                    break;
                case PS2_SC1_B2_KEY_RMETA:
                    keyboard_state.mods ^= PS2_KEY_MOD_RMETA_MASK;
                    break;
                default: break;
            }
            out = ps2_sc1_2byte_translate_lookup[current_byte&(~PS2_SC1_KEY_RELEASED_MASK)] | ((current_byte&PS2_SC1_KEY_RELEASED_MASK)?KEY_RELEASED_MASK:0);
            out |= keyboard_state.mods << 12;
            *internal_state = PS2_NORMAL;
            break;
        case PS2_WAITING_FOR_PAUSE:
            if (ps2_present_devices[device_num-1].scan_code_set == 2 && current_byte == PS2_SC2_KEY_RELEASE_BYTE) {
                *internal_state = PS2_SC2_3B_WAITING_FOR_RELEASE;
                break;
            }
            /* this would cause a random broken scancode from the third byte rather than assuming unknown scancode
                if (current_byte == PS2_SC1_3B_PAUSE_1) *internal_state = PS2_WAITING_FOR_PAUSE_2;
                else *internal_state = PS2_NORMAL;
            */
            *internal_state = PS2_WAITING_FOR_PAUSE_2;
            return;
        case PS2_WAITING_FOR_PAUSE_2:
            if (ps2_present_devices[device_num-1].scan_code_set == 2) {
                if (current_byte == PS2_SC2_3B_PAUSE_2) out = KEY_PAUSE;
            }
            else if ((current_byte & (~PS2_SC1_KEY_RELEASED_MASK)) == PS2_SC1_3B_PAUSE_2) out = KEY_PAUSE | ((current_byte&PS2_SC1_KEY_RELEASED_MASK)?KEY_RELEASED_MASK:0); // note: the pause key return its own release immediately, i though i'd be a good idea to send it too
            // else out = KEY_INVALID; // doesn't matter
            out |= keyboard_state.mods << 12;
            *internal_state = PS2_NORMAL;
            break;
        case PS2_WAITING_FOR_ACK:
            if (current_byte != PS2_RESPONSE_ACK) {
                if (current_byte == PS2_RESPONSE_RESEND_LAST_BYTE) {
                    // resend last command
                }
                else kprintf("Warning: recieved garbage data from %d instead of ACK (recv %hhx)\n", device_num, current_byte);
            }
            *internal_state = PS2_NORMAL;
            return;
        case PS2_WAITING_FOR_ECHO:
            if (current_byte != PS2_RESPONSE_ECHO) {
                kprintf("ERROR: RECIEVED GARBAGE DATA FROM %d INSTEAD OF ECHO (recv %hhx); DISABLING DEVICE\n", device_num, current_byte);
                outb(PS2_COMM_PORT, device_num == 1?PS2_CONTROLLER_COMMAND_DISABLE_PORT1:PS2_CONTROLLER_COMMAND_DISABLE_PORT2);
                ps2_present_devices[device_num-1].present = 0;
                return;
            }
            *internal_state = PS2_NORMAL;
            return;



        case PS2_SC2_WAITING_FOR_RELEASE:
            current_byte = ps2_sc2_to_1_lookup[current_byte];
            current_byte |= PS2_SC1_KEY_RELEASED_MASK;
            *internal_state = PS2_NORMAL;
            goto resolve_key;
        case PS2_SC2_2B_WAITING_FOR_RELEASE:
            current_byte = ps2_sc2_2byte_to_1_lookup[current_byte] | PS2_SC1_KEY_RELEASED_MASK;
            goto resolve_2b_key;
        case PS2_SC2_3B_WAITING_FOR_RELEASE:
            *internal_state = PS2_SC2_WAITING_FOR_RELEASED_PAUSE;
            break;
        case PS2_SC2_WAITING_FOR_RELEASED_PAUSE:
            *internal_state = PS2_SC2_WAITING_FOR_RELEASED_PAUSE_2; // see comment in PS2_WAITING_FOR_PAUSE
            break;
        case PS2_SC2_WAITING_FOR_RELEASED_PAUSE_2:
            if (current_byte == PS2_SC2_3B_PAUSE_2) out = KEY_PAUSE | KEY_RELEASED_MASK;
            out |= keyboard_state.mods << 12;
            *internal_state = PS2_NORMAL;
            break;
        default: break; // mouse commands aren't relevant
    }
    if (out == 0) return;
    if (out == (KEY_DELETE | KEY_MOD_LCONTROL_MASK | KEY_MOD_RALT_MASK)) kernel_reset_system();

    console_translate_scancode(out);
}

static unsigned char mouse_buffer[4]; // 4 in case of 5 button/scroll wheel mouse

#include <errno.h>
static spinlock_t ps2_driver_lock = {0};
static thread_queue_t mouse_queue = {0};

ssize_t ps2_mouse_read(void * buf, size_t n) {
    thread_queue_add(&mouse_queue, current_process, current_thread, SCHED_INTERR_SLEEP);
    if (current_thread->sa_to_be_handled != 0) return -EINTR;

#ifdef PS2_MOUSE_LINUX_COMPAT
    memcpy(buf, mouse_buffer, n > 3 ? 3 : n);
    return n > 3 ? 3 : n;
#else
    memcpy(buf, mouse_buffer, n > 4 ? 4 : n);
    return n > 4 ? 4 : n;
#endif
}

static void ps2_mouse_driver_internal(char device_num) {
    if (!ps2_output_buffer_full(device_num)) {
        kprintf("Warning: Mouse driver got called with data present for a different device, flushing\n");
        inb(PS2_DATA_PORT);
        return;
    }

    uint8_t current_byte = inb(PS2_DATA_PORT);
    if (ps2_present_devices[device_num-1].present == 0) {
        kprintf("Recieved an interrupt for a disabled device! Ignoring\n");
        return;
    }
    enum ps2_internal_states * internal_state;
    if (device_num == 1)
        internal_state = &internal_state_port1;
    else
        internal_state = &internal_state_port2;

    switch (*internal_state) {
        case PS2_NORMAL:
            if (!(current_byte & PS2_MOUSE_P1_ALWAYS_1)) return; // unaligned byte probably
            mouse_buffer[0] = current_byte;
            *internal_state = PS2_MOUSE_WAITING_FOR_BYTE_2;
            return;
        case PS2_MOUSE_WAITING_FOR_BYTE_2:
            mouse_buffer[1] = current_byte;
            *internal_state = PS2_MOUSE_WAITING_FOR_BYTE_3;
            return;
        case PS2_MOUSE_WAITING_FOR_BYTE_3:
            mouse_buffer[2] = current_byte;
            if (ps2_present_devices[device_num-1].type != PS2_DEVICE_STD_MOUSE) {
                *internal_state = PS2_MOUSE_WAITING_FOR_BYTE_4;
                return;
            }
            *internal_state = PS2_MOUSE_NORMAL;
            break;
        case PS2_MOUSE_WAITING_FOR_BYTE_4:
            mouse_buffer[3] = current_byte;
            *internal_state = PS2_MOUSE_NORMAL;
            break;
        default: break;
    }
    switch (ps2_present_devices[device_num-1].type) {
        case PS2_DEVICE_STD_MOUSE:
            mouse_buffer[3] = 0;
            break;
        case PS2_DEVICE_MOUSE_SCROLL:
            mouse_buffer[3] &= 0xF; // to be sure of no garbage data
            break;
        default: break;
    }

    thread_queue_unblock_all(&mouse_queue);
}

volatile char pending_device = -1;

thread_t * ps2_driver_thread = NULL;

static __attribute__((noreturn)) void ps2_driver_loop() {
    kassert(ps2_driver_thread);
    while (1) {
        if (__builtin_expect(pending_device == -1, 0)) {
            ps2_driver_thread->status = SCHED_UNINTERR_SLEEP;
            reschedule();
        }
        else {
            spinlock_acquire(&ps2_driver_lock);
            if (ps2_present_devices[pending_device - 1].is_mouse)
                ps2_mouse_driver_internal(pending_device);
            else
                ps2_keyboard_driver_internal(pending_device);

            if (ps2_present_devices[pending_device - 1].is_mouse)
                pic_send_eoi(PIC_INTERR_PS2_MOUSE);
            else
                pic_send_eoi(PIC_INTERR_KEYBOARD);
            pending_device = -1;
            spinlock_release(&ps2_driver_lock);
        }
    }
}

void ps2_driver(char device_num) {
    if (__builtin_expect(ps2_driver_state == PS2_UNINITIALIZED, 0)) {
        fallback:
        if (ps2_present_devices[device_num-1].is_mouse)
            pic_send_eoi(PIC_INTERR_PS2_MOUSE);
        else
            pic_send_eoi(PIC_INTERR_KEYBOARD);
        return;
    }

    if (__builtin_expect(ps2_driver_state == PS2_INITIALIZED, 0)) {
        ps2_driver_thread = kernel_create_thread(kernel_task, (void (*)(void *))ps2_driver_loop, NULL);
        if (ps2_driver_thread != NULL) ps2_driver_state = PS2_DRIVER_RUNNING;
        else goto fallback;
    }
    asm volatile ("sti");
    //if (pending_device != -1) {
        //kprintf("Warning: PS/2 driver not keeping up with user input!\n");
        while (pending_device != -1) {} // since spinlock_acquire disables interrupts, only way this could happen is in a different core and so this is safe
    //}

    spinlock_acquire(&ps2_driver_lock);
    pending_device = device_num;
    ps2_driver_thread->status = SCHED_RUNNABLE;
    spinlock_release(&ps2_driver_lock);
}

uint32_t scancode_translate_numpad(uint32_t scancode) { // converts numpad to special chars if numlock disabled
    if (scancode & KEY_MOD_NUMLOCK_MASK) return scancode;
    uint32_t new_scancode = 0;
    switch (scancode & KEY_BASE_SCANCODE_MASK) {
        case KEY_KP_7:
            new_scancode = KEY_HOME;
            break;
        case KEY_KP_8:
            new_scancode = KEY_UP;
            break;
        case KEY_KP_9:
            new_scancode = KEY_PAGE_UP;
            break;
        case KEY_KP_4:
            new_scancode = KEY_LEFT;
            break;
        case KEY_KP_5:
            new_scancode = KEY_INVALID;
            break;
        case KEY_KP_6:
            new_scancode = KEY_RIGHT;
            break;
        case KEY_KP_1:
            new_scancode = KEY_END;
            break;
        case KEY_KP_2:
            new_scancode = KEY_DOWN;
            break;
        case KEY_KP_3:
            new_scancode = KEY_PAGE_DOWN;
            break;
        case KEY_KP_0:
            new_scancode = KEY_INSERT;
            break;
        case KEY_KP_DOT:
            new_scancode = KEY_DELETE;
            break;
        default: new_scancode = scancode;
    }
    return (scancode & ~KEY_BASE_SCANCODE_MASK) | (new_scancode & KEY_BASE_SCANCODE_MASK);
}

char is_scancode_mod(uint32_t scancode) {
    switch (scancode & KEY_BASE_SCANCODE_MASK) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
        case KEY_LCONTROL:
        case KEY_RCONTROL:
        case KEY_LALT:
        case KEY_RALT:
        case KEY_LMETA:
        case KEY_RMETA:
            return 1;
        default: return 0;
    }
}

char is_scancode_printable(uint32_t scancode) {
    scancode = scancode & KEY_BASE_SCANCODE_MASK;
    if (scancode >= KEY_MULTIMEDIA_PREV_TRACK) {
        switch (scancode) {
            case KEY_KP_ENTER:
            case KEY_KP_SLASH:
                return 1;
            default: return 0;
        }
    }
    if (scancode >= KEY_CAPSLOCK) {
        if (scancode >= KEY_KP_7 && scancode <= KEY_KP_DOT) return 1;
        return 0;
    }

    switch (scancode) {
        case KEY_LALT:
        case KEY_RSHIFT:
        case KEY_LSHIFT:
        case KEY_LCONTROL:
        case KEY_ESCAPE:
            return 0;
        default: return 1;
    }
}