#pragma once

#include <windows.h>

#include <stdint.h>
#include <stdbool.h>

enum {
    MAI2_IO_OPBTN_TEST = 0x01,
    MAI2_IO_OPBTN_SERVICE = 0x02,
    MAI2_IO_OPBTN_COIN = 0x04,
};

enum {
    MAI2_IO_GAMEBTN_1 = 0x01,
    MAI2_IO_GAMEBTN_2 = 0x02,
    MAI2_IO_GAMEBTN_3 = 0x04,
    MAI2_IO_GAMEBTN_4 = 0x08,
    MAI2_IO_GAMEBTN_5 = 0x10,
    MAI2_IO_GAMEBTN_6 = 0x20,
    MAI2_IO_GAMEBTN_7 = 0x40,
    MAI2_IO_GAMEBTN_8 = 0x80,
    MAI2_IO_GAMEBTN_SELECT = 0x100,
};

/* Get the version of the Maimai IO API that this DLL supports. This
   function should return a positive 16-bit integer, where the high byte is
   the major version and the low byte is the minor version (as defined by the
   Semantic Versioning standard).

   The latest API version as of this writing is 0x0100. */

uint16_t mai2_io_get_api_version(void);

/* Initialize the IO DLL. This is the second function that will be called on
   your DLL, after mai2_io_get_api_version.

   All subsequent calls to this API may originate from arbitrary threads.

   Minimum API version: 0x0100 */

HRESULT mai2_io_init(void);

/* Send any queued outputs (of which there are currently none, though this may
   change in subsequent API versions) and retrieve any new inputs.

   Minimum API version: 0x0100 */

HRESULT mai2_io_poll(void);

/* Get the state of the cabinet's operator buttons as of the last poll. See
   MAI2_IO_OPBTN enum above: this contains bit mask definitions for button
   states returned in *opbtn. All buttons are active-high.

   Minimum API version: 0x0100 */

void mai2_io_get_opbtns(uint8_t *opbtn);

/* Get the state of the cabinet's gameplay buttons as of the last poll. See
   MAI2_IO_GAMEBTN enum above for bit mask definitions. Inputs are split into
   a left hand side set of inputs and a right hand side set of inputs: the bit
   mappings are the same in both cases.

   All buttons are active-high, even though some buttons' electrical signals
   on a real cabinet are active-low.

   Minimum API version: 0x0100 */

void mai2_io_get_gamebtns(uint16_t *player1, uint16_t *player2);

/* Callback function used by mai2_io_touch_1p/2p_thread_proc. 

   The 'player'(1 or 2) parameter indicates which player the touch data is for.

   The 'state' represents a complete response packet.
   The format of the state array is as follows:
   uint8_t state[7] = {
      bytes[0] - bit(0 , 0 , 0 , A5, A4, A3, A2, A1)
      bytes[1] - bit(0 , 0 , 0 , B2, B1, A8, A7, A6)
      bytes[2] - bit(0 , 0 , 0 , B7, B6, B5, B4, B3)
      bytes[3] - bit(0 , 0 , 0 , D2, D1, C2, C1, B8)
      bytes[4] - bit(0 , 0 , 0 , D7, D6, D5, D4, D3)
      bytes[5] - bit(0 , 0 , 0 , E4, E3, E2, E1, D8)
      bytes[6] - bit(0 , 0 , 0 , 0 , E8, E7, E6, E5)
   }
   The 7 bytes are the touch data, with each byte storing the touch state in the lower 5 bits.
   A value of 1 indicates that the corresponding touch area is pressed.
   The touch areas are ordered from A1 to E8, and the binary values are stored from low to high. */

typedef void (*mai2_io_touch_callback_t)(const uint8_t player, const uint8_t state[7]);

/**
 * @brief Initializes the touch input callback function
 *
 * This function accepts a callback function as a parameter and stores it in the global variable `_callback`
 * for later handling of touch input events.
 *
 * @param callback The touch input callback function that takes two parameters: player number and the touch state array.
 * @return HRESULT Returns the result of the operation, S_OK on success.
 */

HRESULT mai2_io_touch_init(mai2_io_touch_callback_t callback);

/* Send sensitivity setting data to the touch device.
   Format:
   bytes[0] - Header
   bytes[1] - Target device, ASCII characters 'L' or 'R'
   bytes[2] - Target touch point
   bytes[3] - commandRatio identifier
   bytes[4] - Ratio value to be set, within a fixed range
   bytes[5] - Footer

   Example function, not actually used. The sensitivity range can be determined
   based on the Ratio set within the game. */

void mai2_io_touch_set_sens(uint8_t *bytes);

/**
 * @brief Updates the touch input acceptance state
 *
 * This function determines whether the game is ready to accept touch input based on the states of player 1 and player 2.
 * If the game is ready, it creates or stops the corresponding threads to handle touch data for each player.
 * Whether or not threads are created for each player is controlled by `mai2_io_cfg.debug_input_1p` and `mai2_io_cfg.debug_input_2p` configuration.
 *
 * @param player1 If `true`, indicates the game is ready to accept touch data from player 1, `false` means the game is not ready.
 * @param player2 If `true`, indicates the game is ready to accept touch data from player 2, `false` means the game is not ready.
 */

void mai2_io_touch_update(bool player1, bool player2);

/**
 * @brief Player touch input handling thread
 *
 * This function runs in a separate thread, continuously monitoring player touch status and passing the state data
 * to the main thread via a callback function. Each time a touch input is detected, it updates the `state` array and calls the callback.
 * The thread stops when `mai2_io_touch_1p/2p_stop_flag` is `true`.
 *
 * @param ctx The callback function context, of type `mai2_io_touch_callback_t`, used to handle the touch input events.
 * @return The thread's return value, typically `0`.
 */

static unsigned int __stdcall mai2_io_touch_1p_thread_proc(void *ctx);

static unsigned int __stdcall mai2_io_touch_2p_thread_proc(void *ctx);

/* Initialize LED emulation. This function will be called before any
   other mai2_io_led_*() function calls.

   All subsequent calls may originate from arbitrary threads and some may
   overlap with each other. Ensuring synchronization inside your IO DLL is
   your responsibility. 
   
   Minimum API version: 0x0101 */

HRESULT mai2_io_led_init(void);

/* Update the FET outputs. rgb is a pointer to an array up to 3 bytes.

   maimai DX uses two boards. Board 0 is for the player 1 side (left) and board 1 
   is for the player 2 side (right).

   Set the brightness of the white light on the machine's outer shell.  
   The program will continuously send changed values to request the blinking effect.

   [0]: BodyLed
   [1]: ExtLed
   [2]: SideLed

   The LED is truned on when the byte is 255 and turned off when the byte is 0.

   Minimum API version: 0x0101 */

void mai2_io_led_set_fet_output(uint8_t board, const uint8_t *rgb);

/* The effect of this command is unknown, it is triggered after LED_15070_CMD_EEPROM_READ. */

void mai2_io_led_dc_update(uint8_t board, const uint8_t *rgb);

/* Update the RGB LEDs. rgb is a pointer to an array up to 8 * 4 = 32 bytes.

   maimai DX uses two boards. Board 0 is for the player 1 side (left) and board 1
   is for the player 2 side (right).

   The LEDs are laid out as follows:
   [0-7]: 8 button LED
 
   Each rgb value is comprised for 4 bytes in the order of R, G, B, Speed.
   Speed is a value from 0 to 255, where 0 is the fastest speed and 255 is the slowest.

   Minimum API version: 0x0101 */

void mai2_io_led_gs_update(uint8_t board, const uint8_t *rgb);
