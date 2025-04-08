/**
 * @file right_side_keyboard.h
 * @brief Header for the right side keyboard STM32 firmware.
 */

#ifndef RIGHT_SIDE_KEYBOARD_H
#define RIGHT_SIDE_KEYBOARD_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

// Define the keyboard state structure
// This will be sent over I2C to the left side
typedef struct {
    // Each bit represents a key state (0 = pressed, 1 = not pressed)
    // For 16 keys, we need 2 bytes
    uint8_t key_states[3];
} RightKeyboardState;

// I2C slave address for this keyboard half
#define RIGHT_KEYBOARD_I2C_ADDRESS 0x42

// Number of keys on the right side
#define NUM_KEYS 24

// Debounce time in milliseconds
#define DEBOUNCE_TIME_MS 10

// Function prototypes
bool RightKeyboardInit(void);
void RightKeyboardScan(RightKeyboardState *state);
void RightKeyboardI2CTransmit(void);

#endif /* RIGHT_SIDE_KEYBOARD_H */
