/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2024 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "pico/stdlib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hid_parser.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pio_usb.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "user_config.h"

/*********  Misc definitions for better readability **********/
#define PICO_A 0
#define PICO_B 1

#define ACTIVE_OUTPUT_A 0
#define ACTIVE_OUTPUT_B 1

#define ENABLE 1
#define DISABLE 0

#define DIRECTION_X 0
#define DIRECTION_Y 1

#define MAX_REPORT_ITEMS 16
#define MOUSE_BOOT_REPORT_LEN 4

#define NUM_SCREENS 2  // Will be more in the future

/*********  Pinout definitions  **********/
#define PIO_USB_DP_PIN 14  // D+ is pin 14, D- is pin 15
#define GPIO_LED_PIN 25    // LED is connected to pin 25 on a PICO

#if BOARD_ROLE == PICO_B
#define SERIAL_TX_PIN 16
#define SERIAL_RX_PIN 17
#elif BOARD_ROLE == PICO_A
#define SERIAL_TX_PIN 12
#define SERIAL_RX_PIN 13
#endif

/*********  Serial port definitions  **********/
#define SERIAL_UART uart0
#define SERIAL_BAUDRATE 3686400

#define SERIAL_DATA_BITS 8
#define SERIAL_STOP_BITS 1
#define SERIAL_PARITY UART_PARITY_NONE

/*********  Watchdog definitions  **********/
#define WATCHDOG_TIMEOUT 500          // In milliseconds => needs to be reset every 500 ms
#define WATCHDOG_PAUSE_ON_DEBUG 1     // When using a debugger, disable watchdog
#define CORE1_HANG_TIMEOUT_US 500000  // In microseconds, wait up to 0.5s to declare core1 dead

/*********  Protocol definitions  *********
 *
 * - every packet starts with 0xAA 0x55 for easy re-sync
 * - then a 1 byte packet type is transmitted
 * - 8 bytes of packet data follows, fixed length for simplicity
 * - 1 checksum byte ends the packet
 *      - checksum includes **only** the packet data
 *      - checksum is simply calculated by XORing all bytes together
 */

enum packet_type_e {
    KEYBOARD_REPORT_MSG = 1,
    MOUSE_REPORT_MSG = 2,
    OUTPUT_SELECT_MSG = 3,
    FIRMWARE_UPGRADE_MSG = 4,
    MOUSE_ZOOM_MSG = 5,
    KBD_SET_REPORT_MSG = 6,
    SWITCH_LOCK_MSG = 7,
    SYNC_BORDERS_MSG = 8,
    FLASH_LED_MSG = 9,
};

/*
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Start1 | Start2 | Type |             Packet data           | Checksum |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    1   |    1   |  1   |                 8                 |     1    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

/* Data structure defining packets of information transferred */
typedef struct {
    uint8_t type;      // Enum field describing the type of packet
    uint8_t data[8];   // Data goes here (type + payload + checksum)
    uint8_t checksum;  // Checksum, a simple XOR-based one
} uart_packet_t;

/*********  Packet parameters  **********/

#define START1 0xAA
#define START2 0x55
#define START_LENGTH 2

#define TYPE_LENGTH 1
#define PACKET_DATA_LENGTH 8  // For simplicity, all packet types are the same length
#define CHECKSUM_LENGTH 1

#define PACKET_LENGTH (TYPE_LENGTH + PACKET_DATA_LENGTH + CHECKSUM_LENGTH)
#define RAW_PACKET_LENGTH (START_LENGTH + PACKET_LENGTH)

#define KBD_QUEUE_LENGTH 128
#define MOUSE_QUEUE_LENGTH 2048

#define KEYS_IN_USB_REPORT 6
#define KBD_REPORT_LENGTH 8
#define MOUSE_REPORT_LENGTH 7

/*********  Screen  **********/
#define MAX_SCREEN_COORD 32767

/*********  Configuration storage definitions  **********/

typedef struct {
    int top;     // When jumping from a smaller to a bigger screen, go to THIS top height
    int bottom;  // When jumping from a smaller to a bigger screen, go to THIS bottom
                 // height
} border_size_t;

/* Define output parameters */
typedef struct {
    int number;           // Number of this output (e.g. ACTIVE_OUTPUT_A = 0 etc)
    int screen_count;     // How many monitors per output (e.g. Output A is Windows with 3 monitors)
    int screen_index;     // Current active screen
    int speed_x;          // Mouse speed per output, in direction X
    int speed_y;          // Mouse speed per output, in direction Y
    border_size_t border; // Screen border size/offset to keep cursor at same height when switching
} output_t;

/* Data structure defining how configuration is stored */
typedef struct {
    uint32_t magic_header;
    uint8_t force_mouse_boot_mode;
    output_t output[NUM_SCREENS];
    uint32_t checksum;
} config_t;

extern const config_t default_config;

extern config_t ADDR_CONFIG[];
#define ADDR_CONFIG_BASE_ADDR (ADDR_CONFIG)

// -------------------------------------------------------+

typedef void (*action_handler_t)();

typedef struct {
    uint8_t modifier;                 // Which modifier is pressed
    uint8_t keys[6];                  // Which keys need to be pressed
    uint8_t key_count;                // How many keys are pressed
    action_handler_t action_handler;  // What to execute when the key combination is detected
    bool pass_to_os;                  // True if we are to pass the key to the OS too
    bool acknowledge;                 // True if we are to notify the user about registering keypress
} hotkey_combo_t;

typedef struct TU_ATTR_PACKED {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t wheel;
    int8_t pan;
} hid_abs_mouse_report_t;

typedef enum { IDLE, READING_PACKET, PROCESSING_PACKET } receiver_state_t;

typedef struct {
    uint8_t kbd_dev_addr;  // Address of the keyboard device
    uint8_t kbd_instance;  // Keyboard instance (d'uh - isn't this a useless comment)

    uint8_t keyboard_leds[NUM_SCREENS];   // State of keyboard LEDs (index 0 = A, index 1 = B)
    uint64_t last_activity[NUM_SCREENS];  // Timestamp of the last input activity (-||-)
    receiver_state_t receiver_state;      // Storing the state for the simple receiver state machine

    uint64_t core1_last_loop_pass;  // Timestamp of last core1 loop execution
    uint8_t active_output;          // Currently selected output (0 = A, 1 = B)

    int16_t mouse_x;  // Store and update the location of our mouse pointer
    int16_t mouse_y;

    config_t config;  // Device configuration, loaded from flash or defaults used

    mouse_t mouse_dev;    // Mouse device specifics, e.g. stores locations for keys in report
    queue_t kbd_queue;    // Queue that stores keyboard reports
    queue_t mouse_queue;  // Queue that stores mouse reports

    /* Connection status flags */
    bool tud_connected;       // True when TinyUSB device successfully connects
    bool keyboard_connected;  // True when our keyboard is connected locally
    bool mouse_connected;     // True when a mouse is connected locally

    /* Feature flags */
    bool mouse_zoom;         // True when "mouse zoom" is enabled
    bool switch_lock;        // True when device is prevented from switching
    bool onboard_led_state;  // True when LED is ON

    /* Onboard LED blinky (provide feedback when e.g. mouse connected) */
    int32_t blinks_left;      // How many blink transitions are left
    int32_t last_led_change;  // Timestamp of the last time led state transitioned

} device_state_t;

/*********  Setup  **********/
void initial_setup(void);
void serial_init(void);
void core1_main(void);

/*********  Keyboard  **********/
bool is_key_pressed(hotkey_combo_t, const hid_keyboard_report_t*);
void process_keyboard_report(uint8_t*, int, device_state_t*);
void stop_pressing_any_keys(device_state_t*);
void queue_kbd_report(hid_keyboard_report_t*, device_state_t*);
void process_kbd_queue_task(device_state_t*);
void send_key(hid_keyboard_report_t*, device_state_t*);

/*********  Mouse  **********/
bool tud_hid_abs_mouse_report(uint8_t report_id,
                              uint8_t buttons,
                              int16_t x,
                              int16_t y,
                              int8_t vertical,
                              int8_t horizontal);

void process_mouse_report(uint8_t*, int, device_state_t*);
uint8_t parse_report_descriptor(mouse_t* mouse,
                                uint8_t arr_count,
                                uint8_t const* desc_report,
                                uint16_t desc_len);
int32_t get_report_value(uint8_t* report, report_val_t* val);
void process_mouse_queue_task(device_state_t*);
void queue_mouse_report(hid_abs_mouse_report_t*, device_state_t*);
void send_mouse(hid_abs_mouse_report_t*, device_state_t*);

/*********  UART  **********/
void receive_char(uart_packet_t*, device_state_t*);
void send_packet(const uint8_t*, enum packet_type_e, int);
void send_value(const uint8_t, enum packet_type_e);

/*********  LEDs  **********/
void restore_leds(device_state_t*);
void blink_led(device_state_t*);
void led_blinking_task(device_state_t*);

/*********  Checksum  **********/
uint8_t calc_checksum(const uint8_t*, int);
bool verify_checksum(const uart_packet_t*);

/*********  Watchdog  **********/
void kick_watchdog(void);

/*********  Configuration  **********/
void load_config(void);
void save_config(void);
void wipe_config(void);

/*********  Handlers  **********/
void output_toggle_hotkey_handler(device_state_t*);
void screen_border_hotkey_handler(device_state_t*);
void fw_upgrade_hotkey_handler_A(device_state_t*);
void fw_upgrade_hotkey_handler_B(device_state_t*);
void mouse_zoom_hotkey_handler(device_state_t*);
void all_keys_released_handler(device_state_t*);
void switchlock_hotkey_handler(device_state_t*);
void wipe_config_hotkey_handler(device_state_t*);

void handle_keyboard_uart_msg(uart_packet_t*, device_state_t*);
void handle_mouse_abs_uart_msg(uart_packet_t*, device_state_t*);
void handle_output_select_msg(uart_packet_t*, device_state_t*);
void handle_mouse_zoom_msg(uart_packet_t*, device_state_t*);
void handle_set_report_msg(uart_packet_t*, device_state_t*);
void handle_switch_lock_msg(uart_packet_t*, device_state_t*);
void handle_sync_borders_msg(uart_packet_t*, device_state_t*);
void handle_flash_led_msg(uart_packet_t*, device_state_t*);
void handle_fw_upgrade_msg(void);

void switch_output(uint8_t);

/*********  Global variables (don't judge)  **********/
extern device_state_t global_state;
