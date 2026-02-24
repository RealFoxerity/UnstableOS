#include "include/kernel_interrupts.h"
#include "include/kernel_sched.h"
#include "include/kernel_spinlock.h"
#include "include/keyboard.h"
#include "include/ps2_keyboard.h"
#include "include/lowlevel.h"
#include "include/kernel.h"
#include <stdint.h>

#define kprintf(x, ...) kprintf("PS/2 driver: "x, ##__VA_ARGS__)

struct ps2_device {
    char present;
    uint8_t type;
    char scan_code_set;
};

static struct ps2_device ps2_present_devices[2];


enum driver_states {
    PS2_UNINITIALIZED,
    PS2_INITIALIZED,
    PS2_DRIVER_RUNNING,
};
enum driver_states ps2_driver_state = PS2_UNINITIALIZED; 

#define PS2_ID_WAIT_AMOUNT 3
#define PS2_RETRY_COUNT 3

static inline char ps2_output_buffer_full() { // there is data to read from device/ctrl
    uint8_t status_reg = inb(PS2_COMM_PORT);
    return (status_reg & PS2_STATUS_OUTPUT_BUFFER_FULL) != 0;
}

static inline char ps2_input_buffer_full() { // clear for writing
    uint8_t status_reg = inb(PS2_COMM_PORT);
    return (status_reg & PS2_STATUS_INPUT_BUFFER_FULL) != 0;
}

static inline void ps2_wait_until_ready_w() { // ready to write
    while (ps2_input_buffer_full());
}

static inline void ps2_wait_until_ready_r() { // ready to read
    while (!ps2_output_buffer_full());
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
        ps2_wait_until_ready_r();
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
        ps2_wait_until_ready_r();
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to enable scanning command, attempt %d/3\n", device_num, i+1);
        }
    }
    return 1;
}

static inline char test_ps2_device(char device_num) {
    if (!ps2_disable_scanning(device_num)) return 0;
    enum ps2_controller_port_test_response test_status;
    outb(PS2_COMM_PORT, device_num == 1?PS2_CONTROLLER_COMMAND_TEST_PORT1:PS2_CONTROLLER_COMMAND_TEST_PORT2);
    ps2_wait_until_ready_r();
    test_status = inb(PS2_DATA_PORT);

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
    return 0;
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
        ps2_wait_until_ready_r();
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
        ps2_wait_until_ready_r();
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to get scancode set command, attempt %d/3\n", device_num, i+1);
        }
    }
    ps2_wait_until_ready_r();
    res_byte = inb(PS2_DATA_PORT);
    if (res_byte != PS2_SCAN_CODE_SET_1) {
        kprintf("Error: PS/2 device %d refused to switch to scancode set 1 (got scs %hhx)\n", device_num, res_byte);
        return 0;
    }
    return 1;
}


static inline void ps2_errored_device(char device_num) {
    kprintf("Disabling device %d\n", device_num);
    outb(PS2_COMM_PORT, device_num == 1 ? PS2_CONTROLLER_COMMAND_DISABLE_PORT1:PS2_CONTROLLER_COMMAND_DISABLE_PORT2);
}

static inline void gather_ps2_device_info(uint8_t device_num) { // todo: implement timer and checking bit 1 of status register
    uint8_t res_byte;
    if (!ps2_disable_scanning(device_num)) {
        errored:
        ps2_errored_device(device_num);
        return;
    }

    for (int i = 0; i < PS2_RETRY_COUNT; i++) {
        if (i == PS2_RETRY_COUNT) goto errored;
        if (device_num == 2) ps2_prepare_send_port_2();
        ps2_wait_until_ready_w();
        outb(PS2_DATA_PORT, PS2_COMMAND_ID_KEYBOARD);
        ps2_wait_until_ready_r();
        if ((res_byte = inb(PS2_DATA_PORT)) == PS2_RESPONSE_ACK) break;
        else if (res_byte != PS2_RESPONSE_RESEND_LAST_BYTE) {
            kprintf("Warning: PS/2 device %d responded with garbage to id command, attempt %d/3\n", device_num, i+1);
        }
    }

    enum ps2_device_types id_major = 0xFF;
    enum ps2_keyboard_types id_minor = 0;

    char have_byte_2 = 0;
    for (int j = 0; j <= PS2_ID_WAIT_AMOUNT; j++) {
        if (j == PS2_ID_WAIT_AMOUNT) {
            kprintf("Unspecified PS/2 device on device number %d, assuming keyboard\n", device_num);
            if (!ps2_keyboard_set_cs1(device_num)) {
                kprintf("Falling back to scancode set 2\n");
                ps2_present_devices[device_num-1].scan_code_set = 2;
            } else ps2_present_devices[device_num-1].scan_code_set = 1;
            goto normal;
        }
        if (ps2_output_buffer_full()) {
            id_major = inb(PS2_DATA_PORT);
            break;
        }
    }

    for (int j = 0; j <= PS2_ID_WAIT_AMOUNT; j++) {
        if (ps2_output_buffer_full()) {
            id_minor = inb(PS2_DATA_PORT);
            have_byte_2 = 1;
            break;
        }
    }

    switch (id_major) {
        case PS2_DEVICE_STD_MOUSE:
            kprintf("Standard mouse on device number %d\n", device_num);
            goto errored; // don't have yet implemented mouse
            break;
        case PS2_DEVICE_MOUSE_SCROLL:
            kprintf("Mouse with scroll wheel on device number %d\n", device_num);
            goto errored;
            break;
        case PS2_DEVICE_5_BUTTON_MOUSE:
            kprintf("5 button mouse on device number %d\n", device_num);
            goto errored;
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

    normal:
    if (!ps2_enable_scanning(device_num)) goto errored;
    ps2_present_devices[device_num - 1].present = 1;
    ps2_present_devices[device_num - 1].type = have_byte_2?id_minor : id_major;
} 

void keyboard_init() {
    uint32_t prev_eflags = 0;
    asm volatile (
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(prev_eflags)
    );

    // implement check acpi

    if (ps2_output_buffer_full()) inb(PS2_DATA_PORT); // discard any remaining data

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_DISABLE_PORT1); // to stop them from sending interrupts
    io_wait();
    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_DISABLE_PORT2);

    outb(PS2_COMM_PORT, PS2_CONTROLLER_COMMAND_READ_BYTE);
    ps2_wait_until_ready_r();
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
    ps2_wait_until_ready_r();
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
    ps2_wait_until_ready_r();
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
        gather_ps2_device_info(1);
        config_byte |= PS2_CONTROLLER_CONFIG_ENABLE_PORT1_INTERRUPT;
        config_byte &= ~PS2_CONTROLLER_CONFIG_DISABLE_PORT1_CLOCK;
    }
    if (valid_devices & 2) {
        gather_ps2_device_info(2);
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

static const uint8_t ps2_sc2_2byte_to_1_lookup[256] = {
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_SEARCH] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_SEARCH,
    [PS2_SC2_B2_KEY_RALT] = PS2_SC1_B2_KEY_RALT,
    [PS2_SC2_B2_KEY_RCONTROL] = PS2_SC1_B2_KEY_RCONTROL,
    [PS2_SC2_B2_KEY_MULTIMEDIA_PREV_TRACK] = PS2_SC1_B2_KEY_MULTIMEDIA_PREV_TRACK,
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_FAVORITES] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_FAVORITES,
    [PS2_SC2_B2_KEY_LMETA] = PS2_SC1_B2_KEY_LMETA,
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_REFRESH] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_REFRESH,
    [PS2_SC2_B2_KEY_MULTIMEDIA_VOLUME_DOWN] = PS2_SC1_B2_KEY_MULTIMEDIA_VOLUME_DOWN,
    [PS2_SC2_B2_KEY_MULTIMEDIA_MUTE] = PS2_SC1_B2_KEY_MULTIMEDIA_MUTE,
    [PS2_SC2_B2_KEY_RMETA] = PS2_SC1_B2_KEY_RMETA,
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_STOP] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_STOP,
    [PS2_SC2_B2_KEY_MULTIMEDIA_CALCULATOR] = PS2_SC1_B2_KEY_MULTIMEDIA_CALCULATOR,
    [PS2_SC2_B2_KEY_COMPOSE] = PS2_SC1_B2_KEY_COMPOSE,
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_FORWARD] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_FORWARD,
    [PS2_SC2_B2_KEY_MULTIMEDIA_VOLUME_UP] = PS2_SC1_B2_KEY_MULTIMEDIA_VOLUME_UP,
    [PS2_SC2_B2_KEY_MULTIMEDIA_PLAY] = PS2_SC1_B2_KEY_MULTIMEDIA_PLAY,
    [PS2_SC2_B2_KEY_ACPI_POWER] = PS2_SC1_B2_KEY_ACPI_POWER,
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_BACK] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_BACK,
    [PS2_SC2_B2_KEY_MULTIMEDIA_WWW_HOME] = PS2_SC1_B2_KEY_MULTIMEDIA_WWW_HOME,
    [PS2_SC2_B2_KEY_MULTIMEDIA_STOP] = PS2_SC1_B2_KEY_MULTIMEDIA_STOP,
    [PS2_SC2_B2_KEY_ACPI_SLEEP] = PS2_SC1_B2_KEY_ACPI_SLEEP,
    [PS2_SC2_B2_KEY_MULTIMEDIA_MY_COMPUTER] = PS2_SC1_B2_KEY_MULTIMEDIA_MY_COMPUTER,
    [PS2_SC2_B2_KEY_MULTIMEDIA_EMAIL] = PS2_SC1_B2_KEY_MULTIMEDIA_EMAIL,
    [PS2_SC2_B2_KEY_KP_SLASH] = PS2_SC1_B2_KEY_KP_SLASH,
    [PS2_SC2_B2_KEY_MULTIMEDIA_NEXT_TRACK] = PS2_SC1_B2_KEY_MULTIMEDIA_NEXT_TRACK,
    [PS2_SC2_B2_KEY_MULTIMEDIA_MEDIA_SELECT] = PS2_SC1_B2_KEY_MULTIMEDIA_MEDIA_SELECT,
    [PS2_SC2_B2_KEY_KP_ENTER] = PS2_SC1_B2_KEY_KP_ENTER,
    [PS2_SC2_B2_KEY_ACPI_WAKE] = PS2_SC1_B2_KEY_ACPI_WAKE,
    [PS2_SC2_B2_KEY_END] = PS2_SC1_B2_KEY_END,
    [PS2_SC2_B2_KEY_LEFT] = PS2_SC1_B2_KEY_LEFT,
    [PS2_SC2_B2_KEY_HOME] = PS2_SC1_B2_KEY_HOME,
    [PS2_SC2_B2_KEY_INSERT] = PS2_SC1_B2_KEY_INSERT,
    [PS2_SC2_B2_KEY_DELETE] = PS2_SC1_B2_KEY_DELETE,
    [PS2_SC2_B2_KEY_DOWN] = PS2_SC1_B2_KEY_DOWN,
    [PS2_SC2_B2_KEY_RIGHT] = PS2_SC1_B2_KEY_RIGHT,
    [PS2_SC2_B2_KEY_UP] = PS2_SC1_B2_KEY_UP,
    [PS2_SC2_B2_KEY_PAGE_DOWN] = PS2_SC1_B2_KEY_PAGE_DOWN,
    [PS2_SC2_B2_KEY_PAGE_UP] = PS2_SC1_B2_KEY_PAGE_UP,
};

static const uint16_t ps2_sc1_2byte_translate_lookup[256] = {
[PS2_SC1_B2_KEY_MULTIMEDIA_PREV_TRACK] = KEY_MULTIMEDIA_PREV_TRACK,
[PS2_SC1_B2_KEY_MULTIMEDIA_NEXT_TRACK] = KEY_MULTIMEDIA_NEXT_TRACK,
[PS2_SC1_B2_KEY_KP_ENTER] = KEY_KP_ENTER,
[PS2_SC1_B2_KEY_RCONTROL] = KEY_RCONTROL,
[PS2_SC1_B2_KEY_MULTIMEDIA_MUTE] = KEY_MULTIMEDIA_MUTE,
[PS2_SC1_B2_KEY_MULTIMEDIA_CALCULATOR] = KEY_MULTIMEDIA_CALCULATOR,
[PS2_SC1_B2_KEY_MULTIMEDIA_PLAY] = KEY_MULTIMEDIA_PLAY,
[PS2_SC1_B2_KEY_MULTIMEDIA_STOP] = KEY_MULTIMEDIA_STOP,
[PS2_SC1_B2_FAKE_LSHIFT] = 0,
[PS2_SC1_B2_KEY_PRINT_SCREEN] = KEY_PRINT_SCREEN,
[PS2_SC1_B2_FAKE_SCROLLOCK] = 0,
[PS2_SC1_B2_KEY_BREAK] = KEY_BREAK,
[PS2_SC1_B2_KEY_MULTIMEDIA_VOLUME_DOWN] = KEY_VOLUME_DOWN,
[PS2_SC1_B2_KEY_MULTIMEDIA_VOLUME_UP] = KEY_VOLUME_UP,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_HOME] = KEY_WWW_HOME,
[PS2_SC1_B2_KEY_KP_SLASH] = KEY_KP_SLASH,
[PS2_SC1_B2_KEY_RALT] = KEY_RALT,
[PS2_SC1_B2_KEY_HOME] = KEY_HOME,
[PS2_SC1_B2_KEY_UP] = KEY_UP,
[PS2_SC1_B2_KEY_PAGE_UP] = KEY_PAGE_UP,
[PS2_SC1_B2_KEY_LEFT] = KEY_LEFT,
[PS2_SC1_B2_KEY_RIGHT] = KEY_RIGHT,
[PS2_SC1_B2_KEY_END] = KEY_END,
[PS2_SC1_B2_KEY_DOWN] = KEY_DOWN,
[PS2_SC1_B2_KEY_PAGE_DOWN] = KEY_PAGE_DOWN,
[PS2_SC1_B2_KEY_INSERT] = KEY_INSERT,
[PS2_SC1_B2_KEY_DELETE] = KEY_DELETE,
[PS2_SC1_B2_KEY_LMETA] = KEY_LMETA,
[PS2_SC1_B2_KEY_RMETA] = KEY_RMETA,
[PS2_SC1_B2_KEY_COMPOSE] = KEY_COMPOSE,
[PS2_SC1_B2_KEY_ACPI_POWER] = KEY_ACPI_POWER,
[PS2_SC1_B2_KEY_ACPI_SLEEP] = KEY_ACPI_SLEEP,
[PS2_SC1_B2_KEY_ACPI_WAKE] = KEY_ACPI_WAKE,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_SEARCH] = KEY_MULTIMEDIA_WWW_SEARCH,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_FAVORITES] = KEY_MULTIMEDIA_WWW_FAVORITES,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_REFRESH] = KEY_MULTIMEDIA_WWW_REFRESH,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_STOP] = KEY_MULTIMEDIA_WWW_STOP,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_FORWARD] = KEY_MULTIMEDIA_WWW_FORWARD,
[PS2_SC1_B2_KEY_MULTIMEDIA_WWW_BACK] = KEY_MULTIMEDIA_WWW_BACK,
[PS2_SC1_B2_KEY_MULTIMEDIA_MY_COMPUTER] = KEY_MULTIMEDIA_MY_COMPUTER,
[PS2_SC1_B2_KEY_MULTIMEDIA_EMAIL] = KEY_MULTIMEDIA_EMAIL,
[PS2_SC1_B2_KEY_MULTIMEDIA_MEDIA_SELECT] = KEY_MULTIMEDIA_MEDIA_SELECT,
};


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
    PS2_SC2_WAITING_FOR_RELEASED_PAUSE, // different state because the release is set in the not always present sencond byte
    PS2_SC2_WAITING_FOR_RELEASED_PAUSE_2
};

static enum ps2_internal_states internal_state_port1 = 0;
static enum ps2_internal_states internal_state_port2 = 0;


struct ps2_keyboard_state {
    uint16_t mods;
    uint8_t leds;
};

static struct ps2_keyboard_state keyboard_state = {0};


static const char ps2_sc2_to_1_lookup[256] = {
    [PS2_SC2_KEY_F9] = KEY_F9,
    [PS2_SC2_KEY_F5] = KEY_F5,
    [PS2_SC2_KEY_F3] = KEY_F3,
    [PS2_SC2_KEY_F1] = KEY_F1,
    [PS2_SC2_KEY_F2] = KEY_F2,
    [PS2_SC2_KEY_F12] = KEY_F12,
    [PS2_SC2_KEY_F10] = KEY_F10,
    [PS2_SC2_KEY_F8] = KEY_F8,
    [PS2_SC2_KEY_F6] = KEY_F6,
    [PS2_SC2_KEY_F4] = KEY_F4,
    [PS2_SC2_KEY_TAB] = KEY_TAB,
    [PS2_SC2_KEY_BACKTICK] = KEY_BACKTICK,
    [PS2_SC2_KEY_LALT] = KEY_LALT,
    [PS2_SC2_KEY_LSHIFT] = KEY_LSHIFT,
    [PS2_SC2_KEY_LCONTROL] = KEY_LCONTROL,
    [PS2_SC2_KEY_Q] = KEY_Q,
    [PS2_SC2_KEY_1] = KEY_1,
    [PS2_SC2_KEY_Z] = KEY_Z,
    [PS2_SC2_KEY_S] = KEY_S,
    [PS2_SC2_KEY_A] = KEY_A,
    [PS2_SC2_KEY_W] = KEY_W,
    [PS2_SC2_KEY_2] = KEY_2,
    [PS2_SC2_KEY_C] = KEY_C,
    [PS2_SC2_KEY_X] = KEY_X,
    [PS2_SC2_KEY_D] = KEY_D,
    [PS2_SC2_KEY_E] = KEY_E,
    [PS2_SC2_KEY_4] = KEY_4,
    [PS2_SC2_KEY_3] = KEY_3,
    [PS2_SC2_KEY_SPACE] = KEY_SPACE,
    [PS2_SC2_KEY_V] = KEY_V,
    [PS2_SC2_KEY_F] = KEY_F,
    [PS2_SC2_KEY_T] = KEY_T,
    [PS2_SC2_KEY_R] = KEY_R,
    [PS2_SC2_KEY_5] = KEY_5,
    [PS2_SC2_KEY_N] = KEY_N,
    [PS2_SC2_KEY_B] = KEY_B,
    [PS2_SC2_KEY_H] = KEY_H,
    [PS2_SC2_KEY_G] = KEY_G,
    [PS2_SC2_KEY_Y] = KEY_Y,
    [PS2_SC2_KEY_6] = KEY_6,
    [PS2_SC2_KEY_M] = KEY_M,
    [PS2_SC2_KEY_J] = KEY_J,
    [PS2_SC2_KEY_U] = KEY_U,
    [PS2_SC2_KEY_7] = KEY_7,
    [PS2_SC2_KEY_8] = KEY_8,
    [PS2_SC2_KEY_COMMA] = KEY_COMMA,
    [PS2_SC2_KEY_K] = KEY_K,
    [PS2_SC2_KEY_I] = KEY_I,
    [PS2_SC2_KEY_O] = KEY_O,
    [PS2_SC2_KEY_0] = KEY_0,
    [PS2_SC2_KEY_9] = KEY_9,
    [PS2_SC2_KEY_DOT] = KEY_DOT,
    [PS2_SC2_KEY_SLASH] = KEY_SLASH,
    [PS2_SC2_KEY_L] = KEY_L,
    [PS2_SC2_KEY_SEMICOLON] = KEY_SEMICOLON,
    [PS2_SC2_KEY_P] = KEY_P,
    [PS2_SC2_KEY_MINUS] = KEY_MINUS,
    [PS2_SC2_KEY_APOSTROPHE] = KEY_APOSTROPHE,
    [PS2_SC2_KEY_LBRACE] = KEY_LBRACE,
    [PS2_SC2_KEY_EQUAL] = KEY_EQUAL,
    [PS2_SC2_KEY_CAPSLOCK] = KEY_CAPSLOCK,
    [PS2_SC2_KEY_RSHIFT] = KEY_RSHIFT,
    [PS2_SC2_KEY_ENTER] = KEY_ENTER,
    [PS2_SC2_KEY_RBRACE] = KEY_RBRACE,
    [PS2_SC2_KEY_BACKSLASH] = KEY_BACKSLASH,
    [PS2_SC2_KEY_BACKSPACE] = KEY_BACKSPACE,
    [PS2_SC2_KEY_KP_1] = KEY_KP_1,
    [PS2_SC2_KEY_KP_4] = KEY_KP_4,
    [PS2_SC2_KEY_KP_7] = KEY_KP_7,
    [PS2_SC2_KEY_KP_0] = KEY_KP_0,
    [PS2_SC2_KEY_KP_DOT] = KEY_KP_DOT,
    [PS2_SC2_KEY_KP_2] = KEY_KP_2,
    [PS2_SC2_KEY_KP_5] = KEY_KP_5,
    [PS2_SC2_KEY_KP_6] = KEY_KP_6,
    [PS2_SC2_KEY_KP_8] = KEY_KP_8,
    [PS2_SC2_KEY_ESCAPE] = KEY_ESCAPE,
    [PS2_SC2_KEY_NUMLOCK] = KEY_NUMLOCK,
    [PS2_SC2_KEY_F11] = KEY_F11,
    [PS2_SC2_KEY_KP_PLUS] = KEY_KP_PLUS,
    [PS2_SC2_KEY_KP_3] = KEY_KP_3,
    [PS2_SC2_KEY_KP_MINUS] = KEY_KP_MINUS,
    [PS2_SC2_KEY_KP_ASTERISK] = KEY_KP_ASTERISK,
    [PS2_SC2_KEY_KP_9] = KEY_KP_9,
    [PS2_SC2_KEY_SCROLLLOCK] = KEY_SCROLLLOCK,
    [PS2_SC2_KEY_F7] = KEY_F7,
};


extern void tty_console_input(uint32_t scancode);


static void keyboard_driver_internal(char device_num) {
    uint32_t out = 0;

    uint8_t current_byte = inb(PS2_DATA_PORT);
    if (ps2_present_devices[device_num-1].present == 0) {
        kprintf("Recieved an interrupt for a disabled device! Ignoring\n");
        goto exit;
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
            goto exit;
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
            goto exit;
        case PS2_WAITING_FOR_ECHO:
            if (current_byte != PS2_RESPONSE_ECHO) {
                kprintf("ERROR: RECIEVED GARBAGE DATA FROM %d INSTEAD OF ECHO (recv %hhx); DISABLING DEVICE\n", device_num, current_byte);
                outb(PS2_COMM_PORT, device_num == 1?PS2_CONTROLLER_COMMAND_DISABLE_PORT1:PS2_CONTROLLER_COMMAND_DISABLE_PORT2);
                ps2_present_devices[device_num-1].present = 0;
                goto exit;
            }
            *internal_state = PS2_NORMAL;
            goto exit;

        

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
    }
    
    if (out == (KEY_DELETE | KEY_MOD_LCONTROL_MASK | KEY_MOD_RALT_MASK)) kernel_reset_system();

    tty_console_input(out);

    exit:
    pic_send_eoi(PIC_INTERR_KEYBOARD);
}

spinlock_t ps2_pending_lock = {0};
char pending_device = -1;

thread_t * ps2_driver_thread = NULL;

static void keyboard_driver_loop() {
    kassert(ps2_driver_thread);
    while (1) {
        if (__builtin_expect(pending_device == -1, 0)) reschedule();
        else {
            spinlock_acquire(&ps2_pending_lock);
            keyboard_driver_internal(pending_device);
            pending_device = -1;
            ps2_driver_thread->status = SCHED_UNINTERR_SLEEP; // thread locking shouldn't be needed if we lock the ps2_pending_lock each time
            spinlock_release(&ps2_pending_lock);
        }
    }
}

void keyboard_driver(char device_num) {
    if (__builtin_expect(ps2_driver_state == PS2_UNINITIALIZED, 0)) {
        pic_send_eoi(PIC_INTERR_KEYBOARD);
        return;
    }
    if (__builtin_expect(ps2_driver_state == PS2_INITIALIZED, 0)) {
        ps2_driver_thread = kernel_create_thread(kernel_task, keyboard_driver_loop, NULL);
        ps2_driver_state = PS2_DRIVER_RUNNING;
    }
    if (pending_device != -1) {
        kprintf("Warning: PS/2 driver not keeping up with user input!\n");
        while (pending_device != -1); // since spinlock_acquire disables interrupts, only way this could happen is in a different core and so this is safe
    }

    spinlock_acquire(&ps2_pending_lock);
    pending_device = device_num;
    ps2_driver_thread->status = SCHED_RUNNABLE;
    spinlock_release(&ps2_pending_lock);
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