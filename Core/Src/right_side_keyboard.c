/**
 * @file right_side_keyboard.c
 * @brief Implementation for the right side keyboard STM32 firmware.
 */

#include "right_side_keyboard.h"

// Global keyboard state
static RightKeyboardState keyboard_state;

// Define the GPIO pins for each key
// Each key has its own dedicated pin
static const uint16_t KEY_PINS[NUM_KEYS] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,  // Keys 0-3 on GPIOA
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7,  // Keys 4-7 on GPIOA
    GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10, GPIO_PIN_11, // Keys 8-11 on GPIOA
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,  // Keys 12-15 on GPIOB
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
static uint32_t last_debounce_time[NUM_KEYS] = {0};
static GPIO_PinState last_key_state[NUM_KEYS] = {GPIO_PIN_SET};
static GPIO_PinState debounced_key_state[NUM_KEYS] = {GPIO_PIN_SET};

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
        
        // Initialize debounce tracking
        last_key_state[i] = GPIO_PIN_SET;        // Not pressed
        debounced_key_state[i] = GPIO_PIN_SET;   // Not pressed
        last_debounce_time[i] = 0;
    }
    
    // Set up I2C in slave mode with the correct address
    // Note: The I2C initialization is done in MX_I2C1_Init() in main.c
    // We just need to set the slave address
    hi2c1.Init.OwnAddress1 = RIGHT_KEYBOARD_I2C_ADDRESS << 1; // Shift left because HAL expects 7-bit address in upper bits
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        return false;
    }
    
    // Start listening for I2C master requests
    if (HAL_I2C_Slave_Receive_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state)) != HAL_OK) {
        return false;
    }
    
    return true;
}

void RightKeyboardScan(RightKeyboardState *state)
{
    if (!state) {
        return;
    }

    uint32_t current_time = HAL_GetTick();
    
    // Read each key directly
    for (int i = 0; i < NUM_KEYS; i++) {
        // Read the current state of the key
        GPIO_PinState current_state = HAL_GPIO_ReadPin(KEY_PORTS[i], KEY_PINS[i]);

        // TODO: REWORK DEBOUNCE TIMER TO NOT INTRODUCE DELAY BEFORE TRANSMISSION
        // If the state changed, reset the debounce timer
        if (current_state != last_key_state[i]) {
            last_debounce_time[i] = current_time;
        }

        // If the state has been stable for the debounce period, update the debounced state
        if ((current_time - last_debounce_time[i]) > DEBOUNCE_TIME_MS) {
            // Only update if the debounced state is different
            if (debounced_key_state[i] != current_state) {
                debounced_key_state[i] = current_state;
            }
        }

        // Save the current state for next comparison
        last_key_state[i] = current_state;
        
        // Update the key state in the state structure
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        
        if (debounced_key_state[i] == GPIO_PIN_RESET) {
            // Key is pressed (active low), clear the bit (0 = pressed)
            state->key_states[byteIndex] &= ~(1 << bitIndex);
        } else {
            // Key is not pressed, set the bit (1 = not pressed)
            state->key_states[byteIndex] |= (1 << bitIndex);
        }
    }
}

// This function would be called by the I2C interrupt handler when the master
// requests data
void RightKeyboardI2CTransmit(void)
{
    // Scan the keyboard to get the latest state
    RightKeyboardScan(&keyboard_state);
    
    // Prepare to transmit data when requested by the master
    HAL_I2C_Slave_Transmit_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state));
}

// I2C event callback - will be called by HAL when I2C events occur
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        // Transmission complete, prepare for next request
        HAL_I2C_Slave_Receive_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state));
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
        // Error occurred, reset I2C
        HAL_I2C_Slave_Receive_IT(&hi2c1, (uint8_t*)&keyboard_state, sizeof(keyboard_state));
    }
}
