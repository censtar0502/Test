#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "main.h"

#define KEY_ROWS 5
#define KEY_COLS 4

void Keyboard_Init(void);
char Keyboard_Scan(void);
char Keyboard_GetKey(void); // Returns key only once per press (debounced)

#endif // KEYBOARD_H
