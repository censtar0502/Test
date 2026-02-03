#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "main.h"

typedef enum {
    UI_STATE_MAIN,
    UI_STATE_TOTALIZER,
    UI_STATE_SET_PRICE,
    UI_STATE_INPUT_VOLUME,
    UI_STATE_INPUT_AMOUNT,
    UI_STATE_FUELLING,
    UI_STATE_TRANSACTION_RESULT
} UI_State_t;

void UI_Init(void);
void UI_ProcessInput(void);  // Process keyboard input (call every loop)
void UI_Draw(void);          // Draw screen (call only when display ready)

#endif // UI_MANAGER_H
