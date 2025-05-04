/**
 * @file right_side_keyboard.c
 * @brief Implementation for the right side keyboard STM32 firmware.
 */

#include "right_side_keyboard.h"

#include <string.h>

// Global keyboard state
static RightKeyboardState keyboard_state;

// Define the GPIO pins for each key
// Each key has its own dedicated pin
static const uint16_t KEY_PINS[NUM_KEYS] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,  // Keys 0-3 on GPIOA
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7,  // Keys 4-7 on GPIOA
    GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10, GPIO_PIN_11, // Keys 8-11 on GPIOA
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_15,  // Keys 12-15 on GPIOB
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_8, GPIO_PIN_9,   // Keys 16-20 on GPIOB
    GPIO_PIN_10, GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14 // Keys 20-24 on GPIOB
};

static GPIO_TypeDef* const KEY_PORTS[NUM_KEYS] = {
    GPIOA, GPIOA, GPIOA, GPIOA,  // Keys 0-3
    GPIOA, GPIOA, GPIOA, GPIOA,  // Keys 4-7
    GPIOA, GPIOA, GPIOA, GPIOA,  // Keys 8-11
    GPIOB, GPIOB, GPIOB, GPIOB,  // Keys 12-15
    GPIOB, GPIOB, GPIOB, GPIOB,   // Keys 16-20
    GPIOB, GPIOB, GPIOB, GPIOB   // Keys 21-24
};

// Debounce tracking
static uint32_t      lockout_until[NUM_KEYS]       = {0};       /* time-stamp of last accepted edge + DEBOUNCE */
static GPIO_PinState debounced_state[NUM_KEYS]     = {GPIO_PIN_SET};

// I2C handle from main.c
extern I2C_HandleTypeDef hi2c1;

bool RightKeyboardInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Clear the keyboard state
    for (int i = 0; i < sizeof(keyboard_state.key_states); i++) {
        keyboard_state.key_states[i] = 0xFF; // All keys not pressed (1 = not pressed)
    }
    
    // Configure all key pins as inputs with pull-up
    for (int i = 0; i < NUM_KEYS; i++) {
        GPIO_InitStruct.Pin = KEY_PINS[i];
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(KEY_PORTS[i], &GPIO_InitStruct);
    }
    
    // Set up I2C in slave mode with the correct address
    // Note: The I2C initialization is done in MX_I2C1_Init() in main.c
    // We just need to set the slave address
    hi2c1.Init.OwnAddress1 = RIGHT_KEYBOARD_I2C_ADDRESS << 1; // Shift left because HAL expects 7-bit address in upper bits
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        return false;
    }
    
    // Update the keyboard state with initial scan
    RightKeyboardScan6KRO(&keyboard_state, 6);
    
    // Start listening for I2C master requests by setting up the transmit buffer
    // This ensures the slave is ready to respond when the master initiates a read request
    if (HAL_I2C_Slave_Transmit_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state)) != HAL_OK) {
        return false;
    }
    
    return true;
}

/**
 * Original keyboard scan function - calls the optimized version
 */
void RightKeyboardScan(RightKeyboardState *state)
{
    // Call the optimized version with no key limit (scan all keys)
    RightKeyboardScan6KRO(state, 0);
}

/**
 * Optimized keyboard scan function with early-exit capability for 6KRO
 *
 * @param state Pointer to keyboard state structure to fill
 * @param max_keys Maximum number of keys to scan for (0 means scan all)
 */
void RightKeyboardScan6KRO(RightKeyboardState *state, uint8_t max_keys) {
    if (!state) return;

    uint32_t now = HAL_GetTick();
    uint8_t pressed_count = 0;

    /* start with "all released" */
    memset(state->key_states, 0xFF, sizeof(state->key_states));

    // Optimize port access - cache GPIOx->IDR register values
    uint32_t gpio_a_state = GPIOA->IDR;
    uint32_t gpio_b_state = GPIOB->IDR;

    // For each GPIO port (A and B), we'll batch process the keys
    // This reduces individual HAL_GPIO_ReadPin calls with direct bit testing
    for (int i = 0; i < NUM_KEYS; ++i) {
        // 1) Sample the raw pin (direct bit testing instead of HAL call)
        GPIO_PinState raw;
        if (KEY_PORTS[i] == GPIOA) {
            raw = (gpio_a_state & KEY_PINS[i]) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        } else { // GPIOB
            raw = (gpio_b_state & KEY_PINS[i]) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        }

        // 2) Immediate edge + lock-out debounce
        //    - Accept any transition (press or release) instantly
        //    - Then ignore further changes for DEBOUNCE_TIME_MS
        if (raw != debounced_state[i] && now >= lockout_until[i]) {
            debounced_state[i] = raw;
            lockout_until[i] = now + DEBOUNCE_TIME_MS;
        }

        // 3) Map debounced state into the report (0 = pressed, 1 = released)
        if (debounced_state[i] == GPIO_PIN_RESET) {
            // Use bit shifts instead of division and modulo
            uint8_t byte_idx = i >> 3;      // i / 8
            uint8_t bit_pos = i & 0x07;     // i % 8
            state->key_states[byte_idx] &= ~(1u << bit_pos);

            // Count pressed keys for early-exit
            pressed_count++;

            // Early-exit if we've reached the maximum specified key count
            // and it's not zero (which means no limit)
            if (max_keys > 0 && pressed_count >= max_keys) {
                break;
            }
        }
    }
}

// This function would be called by the I2C interrupt handler when the master
// requests data
void RightKeyboardI2CTransmit(void)
{
    // Scan the keyboard to get the latest state
    RightKeyboardScan6KRO(&keyboard_state, 6);
    
    // Prepare to transmit data when requested by the master
    HAL_I2C_Slave_Transmit_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state));
}

// I2C event callback - will be called by HAL when I2C events occur
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        // Transmission complete, scan keyboard and prepare for next transmit request
        RightKeyboardScan6KRO(&keyboard_state, 6);
        HAL_I2C_Slave_Transmit_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state));
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        // Received a request from master, prepare to transmit
        RightKeyboardI2CTransmit();
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        // Error occurred, reset I2C and prepare for next transmission
        RightKeyboardScan6KRO(&keyboard_state, 6);
        HAL_I2C_Slave_Transmit_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state));
    }
}