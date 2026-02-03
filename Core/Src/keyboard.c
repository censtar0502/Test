#include "keyboard.h"

static const char key_map[KEY_ROWS][KEY_COLS] = {
    {'H', 'G', 'F', 'A'},
    {'3', '2', '1', 'B'},
    {'6', '5', '4', 'C'},
    {'9', '8', '7', 'D'},
    {'K', '0', '.', 'E'}
};

static GPIO_TypeDef* row_ports[KEY_ROWS] = {
    KeyRow_1_GPIO_Port, KeyRow_2_GPIO_Port, KeyRow_3_GPIO_Port, KeyRow_4_GPIO_Port, KeyRow_5_GPIO_Port
};

static uint16_t row_pins[KEY_ROWS] = {
    KeyRow_1_Pin, KeyRow_2_Pin, KeyRow_3_Pin, KeyRow_4_Pin, KeyRow_5_Pin
};

static GPIO_TypeDef* col_ports[KEY_COLS] = {
    KeyCol_1_GPIO_Port, KeyCol_2_GPIO_Port, KeyCol_3_GPIO_Port, KeyCol_4_GPIO_Port
};

static uint16_t col_pins[KEY_COLS] = {
    KeyCol_1_Pin, KeyCol_2_Pin, KeyCol_3_Pin, KeyCol_4_Pin
};

static char last_key = 0;
static uint32_t last_scan_time = 0;

void Keyboard_Init(void) {
    for (uint8_t i = 0; i < KEY_ROWS; i++) {
        HAL_GPIO_WritePin(row_ports[i], row_pins[i], GPIO_PIN_SET);
    }
}

char Keyboard_Scan(void) {
    for (uint8_t r = 0; r < KEY_ROWS; r++) {
        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_RESET);
        
        for(volatile int i=0; i<50; i++);

        for (uint8_t c = 0; c < KEY_COLS; c++) {
            if (HAL_GPIO_ReadPin(col_ports[c], col_pins[c]) == GPIO_PIN_RESET) {
                HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
                return key_map[r][c];
            }
        }
        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
    }
    return 0;
}

char Keyboard_GetKey(void) {
    uint32_t now = HAL_GetTick();
    
    // ИСПРАВЛЕНИЕ #5: Убрана избыточная проверка переполнения
    // Unsigned arithmetic автоматически обрабатывает wrap-around
    if ((now - last_scan_time) >= 50) {
        last_scan_time = now;
        
        char current_key = Keyboard_Scan();
        
        if (current_key != last_key) {
            last_key = current_key;
            if (current_key != 0) {
                return current_key;
            }
        }
    }
    
    return 0;
}
