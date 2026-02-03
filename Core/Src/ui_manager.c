#include "ui_manager.h"
#include "ssd1309.h"
#include "keyboard.h"
#include "dispenser.h"
#include "eeprom_at24.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void UsbLog_Printf(const char *fmt, ...);

// ============================================================================
// НОВАЯ ФУНКЦИЯ: Парсинг дробного числа "5.5" → 550 сантилитров
// ============================================================================
static uint32_t ParseDecimalVolume(const char* str) {
    if (!str || str[0] == '\0') return 0;

    const char* dot_pos = strchr(str, '.');

    if (dot_pos == NULL) {
        // Нет точки - целое число
        uint32_t liters = (uint32_t)atol(str);
        return liters * 100;
    }

    // Есть точка - парсим целую и дробную части
    char integer_part[16] = {0};
    char decimal_part[16] = {0};

    size_t int_len = dot_pos - str;
    if (int_len > 0 && int_len < sizeof(integer_part)) {
        memcpy(integer_part, str, int_len);
    }

    const char* dec_start = dot_pos + 1;
    size_t dec_len = strlen(dec_start);
    if (dec_len > 0 && dec_len < sizeof(decimal_part)) {
        memcpy(decimal_part, dec_start, dec_len);
    }

    uint32_t liters = (uint32_t)atol(integer_part);
    uint32_t decimal = (uint32_t)atol(decimal_part);

    if (dec_len == 1) {
        decimal *= 10;  // "5.5" → 50 сотых
    } else if (dec_len > 2) {
        decimal = decimal / 10;  // Ограничение 2 цифры
    }

    if (decimal > 99) decimal = 99;

    return liters * 100 + decimal;
}

extern SSD1309_t oled;
static UI_State_t ui_state = UI_STATE_MAIN;
static UI_State_t prev_transaction_mode = UI_STATE_INPUT_VOLUME;
static uint32_t global_price = 0;
#define INPUT_BUF_MAX_CHARS 10  // Максимум символов для ввода (не включая '\0')
static char input_buf[INPUT_BUF_MAX_CHARS + 1];  // +1 для '\0'
static uint8_t input_pos = 0;
static uint32_t last_ui_draw_tick = 0;

static uint32_t target_volume_cl = 0;
static uint32_t target_amount = 0;
static uint32_t transaction_end_tick = 0;
static uint8_t transaction_closed = 0;
static uint32_t fuelling_entry_tick = 0;

#define FUELLING_TIMEOUT_MS 60000

// Variables for error message display
static char error_message[32];
static uint32_t error_display_start = 0;
static uint32_t error_display_duration = 3000;  // 3 seconds

void UI_Init(void) {
    Keyboard_Init();
    Dispenser_Init();
    global_price = EEPROM_LoadPrice();
    
    // Строгая валидация с диапазоном 0-9999
    if (global_price > 9999) {
        UsbLog_Printf("WARNING: Invalid price from EEPROM: %lu, using default 1100\r\n", 
                     (unsigned long)global_price);
        global_price = 1100;
        
        // Сохраняем корректное значение обратно в EEPROM
        EEPROM_SavePrice(global_price);
    } else {
        UsbLog_Printf("Loaded price from EEPROM: %lu\r\n", (unsigned long)global_price);
    }
    memset(input_buf, 0, sizeof(input_buf));
    input_pos = 0;
    target_volume_cl = 0;
    target_amount = 0;
}

static void DrawMain(void) {
    char buf[20];
    SSD1309_Clear(&oled);
    
    if (g_dispenser.status == DS_CALLING) {
        SSD1309_DrawString8x8(&oled, 112, 0, "*", SSD1309_COLOR_WHITE);
    }
    
    SSD1309_DrawString8x8(&oled, 0, 0,  "MAIN MENU", SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }
    sprintf(buf, "Price: %u", (unsigned int)global_price);
    SSD1309_DrawString8x8(&oled, 0, 16, buf, SSD1309_COLOR_WHITE);

    SSD1309_DrawString8x8(&oled, 0, 32, "TOT:Totalal", SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 48, "CAL:Vol SEL:Amt", SSD1309_COLOR_WHITE);

    SSD1309_UpdateAsync(&oled);
}

static void DrawTotalizer(void) {
    char buf[20];
    SSD1309_Clear(&oled);
    SSD1309_DrawString8x8(&oled, 0, 0, "TOTALIZER", SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }
    sprintf(buf, "TOT: %u.%02u", 
        (unsigned int)(g_dispenser.totalizer / 100), 
        (unsigned int)(g_dispenser.totalizer % 100));
    SSD1309_DrawString8x8(&oled, 0, 24, buf, SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 48, "ESC: Back", SSD1309_COLOR_WHITE);
    SSD1309_UpdateAsync(&oled);
}

static void DrawSetPrice(void) {
    SSD1309_Clear(&oled);
    SSD1309_DrawString8x8(&oled, 0, 0, "SET PRICE:", SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }
    SSD1309_DrawString8x8(&oled, 0, 16, input_buf, SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 32, "OK:OK RES:Clr", SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 48, "ESC:Exit", SSD1309_COLOR_WHITE);
    SSD1309_UpdateAsync(&oled);
}

// ВСЁ ОСТАЁТСЯ КАК БЫЛО - БЕЗ ИЗМЕНЕНИЙ!
static void DrawInputVolume(void) {
    SSD1309_Clear(&oled);
    SSD1309_DrawString8x8(&oled, 0, 0, "VOLUME...", SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }

    SSD1309_DrawString8x8(&oled, 0, 16, input_buf, SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 32, "OK:OK RES:Clr", SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 48, "ESC:Exit", SSD1309_COLOR_WHITE);
    SSD1309_UpdateAsync(&oled);
}

// ВСЁ ОСТАЁТСЯ КАК БЫЛО - БЕЗ ИЗМЕНЕНИЙ!
static void DrawInputAmount(void) {
    SSD1309_Clear(&oled);
    SSD1309_DrawString8x8(&oled, 0, 0, "AMOUNT...", SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }
    SSD1309_DrawString8x8(&oled, 0, 16, input_buf, SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 32, "OK:OK RES:Clr", SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 48, "ESC:Exit", SSD1309_COLOR_WHITE);
    SSD1309_UpdateAsync(&oled);
}

static void DrawFuelling(void) {
    char buf[20];
    SSD1309_Clear(&oled);
    
    const char* st = "????";
    switch (g_dispenser.status) {
        case DS_IDLE: st = "IDLE"; break;
        case DS_CALLING: st = "CALL"; break;
        case DS_AUTHORIZED: st = "AUTH"; break;
        case DS_STARTED: st = "START"; break;
        case DS_FUELLING: st = "FUEL"; break;
        case DS_STOP: st = "STOP"; break;
        case DS_END: st = "END"; break;
        default: st = "WAIT"; break;
    }
    
    SSD1309_DrawString8x8(&oled, 0, 0, st, SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }

    sprintf(buf, "L:%u.%02u", 
        (unsigned int)(g_dispenser.volume_cl / 100), 
        (unsigned int)(g_dispenser.volume_cl % 100));
    SSD1309_DrawString8x8(&oled, 0, 16, buf, SSD1309_COLOR_WHITE);
    
    sprintf(buf, "A:%u", (unsigned int)g_dispenser.amount);
    SSD1309_DrawString8x8(&oled, 0, 32, buf, SSD1309_COLOR_WHITE);
    
    uint8_t progress_percent = 0;
    if (prev_transaction_mode == UI_STATE_INPUT_VOLUME && target_volume_cl > 0) {
        progress_percent = (g_dispenser.volume_cl * 100) / target_volume_cl;
    } else if (prev_transaction_mode == UI_STATE_INPUT_AMOUNT && target_amount > 0) {
        progress_percent = (g_dispenser.amount * 100) / target_amount;
    }
    if (progress_percent > 100) progress_percent = 100;
    
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 56, SSD1309_COLOR_WHITE);
        SSD1309_DrawPixel(&oled, x, 63, SSD1309_COLOR_WHITE);
    }
    for (int y = 56; y <= 63; y++) {
        SSD1309_DrawPixel(&oled, 0, y, SSD1309_COLOR_WHITE);
        SSD1309_DrawPixel(&oled, 127, y, SSD1309_COLOR_WHITE);
    }
    
    uint8_t fill_width = (progress_percent * 125) / 100;
    for (int x = 1; x <= fill_width && x < 127; x++) {
        for (int y = 57; y <= 62; y++) {
            SSD1309_DrawPixel(&oled, x, y, SSD1309_COLOR_WHITE);
        }
    }
    
    SSD1309_UpdateAsync(&oled);
}

static void DrawTransactionResult(void) {
    char buf[20];
    SSD1309_Clear(&oled);
    SSD1309_DrawString8x8(&oled, 0, 0, "TRANSACTION END", SSD1309_COLOR_WHITE);
    for (int x = 0; x < 128; x++) {
        SSD1309_DrawPixel(&oled, x, 9, SSD1309_COLOR_WHITE);
    }
    
    sprintf(buf, "L:%u.%02u", 
        (unsigned int)(g_dispenser.volume_cl / 100), 
        (unsigned int)(g_dispenser.volume_cl % 100));
    SSD1309_DrawString8x8(&oled, 0, 16, buf, SSD1309_COLOR_WHITE);
    
    sprintf(buf, "A:%u", (unsigned int)g_dispenser.amount);
    SSD1309_DrawString8x8(&oled, 0, 32, buf, SSD1309_COLOR_WHITE);
    
    SSD1309_DrawString8x8(&oled, 0, 48, "ESC:Repeat", SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 56, "RES:Menu", SSD1309_COLOR_WHITE);
    SSD1309_UpdateAsync(&oled);
}

// Функция для отображения сообщения об ошибке
static void ShowErrorMessage(const char* msg) {
    strncpy(error_message, msg, sizeof(error_message) - 1);
    error_message[sizeof(error_message) - 1] = '\0';  // Гарантируем null-терминатор
    error_display_start = HAL_GetTick();
    ui_state = UI_STATE_ERROR_MESSAGE;
}

static void DrawErrorMessage(void) {
    SSD1309_Clear(&oled);
    SSD1309_DrawString8x8(&oled, 0, 8, "ERROR", SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 32, error_message, SSD1309_COLOR_WHITE);
    SSD1309_DrawString8x8(&oled, 0, 56, "Press any key...", SSD1309_COLOR_WHITE);
    SSD1309_UpdateAsync(&oled);
}

void UI_ProcessInput(void) {
    char key = Keyboard_GetKey();
    if (key != 0) {
        UsbLog_Printf("Key: %c\r\n", key);
    }
    
    switch (ui_state) {
        case UI_STATE_MAIN:
            if (key == 'A') {
                Dispenser_RequestTotalizer();
                ui_state = UI_STATE_TOTALIZER;
            } else if (key == 'B') {
                ui_state = UI_STATE_INPUT_VOLUME;
                prev_transaction_mode = UI_STATE_INPUT_VOLUME;
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            } else if (key == 'C') {
                ui_state = UI_STATE_INPUT_AMOUNT;
                prev_transaction_mode = UI_STATE_INPUT_AMOUNT;
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            } else if (key == 'G') {
                ui_state = UI_STATE_SET_PRICE;
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            }
            break;
            
        case UI_STATE_TOTALIZER:
            if (key == 'F') {
                ui_state = UI_STATE_MAIN;
            }
            break;
            
        case UI_STATE_SET_PRICE:
            if (key >= '0' && key <= '9') {
                if (input_pos < 10) {
                    input_buf[input_pos++] = key;
                    input_buf[input_pos] = '\0';
                }
            } else if (key == 'K') {
                uint32_t new_price = atol(input_buf);
                if (new_price <= 9999) {
                    global_price = new_price;
                    EEPROM_SavePrice(global_price);
                    UsbLog_Printf("Price set to: %lu\r\n", (unsigned long)global_price);
                    ui_state = UI_STATE_MAIN;
                } else {
                    UsbLog_Printf("ERROR: Price must be 0-9999, got: %lu\r\n", (unsigned long)new_price);
                    // Оставляемся в том же состоянии для повторного ввода
                }
            } else if (key == 'E') {
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            } else if (key == 'F') {
                ui_state = UI_STATE_MAIN;
            }
            break;
            
        case UI_STATE_INPUT_VOLUME:
            if (key >= '0' && key <= '9') {
                if (input_pos < INPUT_BUF_MAX_CHARS) {
                    input_buf[input_pos++] = key;
                    input_buf[input_pos] = '\0';
                }
            }
            // ✅ НОВОЕ: Обработка точки
            else if (key == '.') {
                if (strchr(input_buf, '.') == NULL && input_pos > 0 && input_pos < INPUT_BUF_MAX_CHARS) {
                    input_buf[input_pos++] = '.';
                    input_buf[input_pos] = '\0';
                }
            }
            else if (key == 'K') {
                // ✅ ИЗМЕНЕНО: Используем ParseDecimalVolume
                uint32_t volume_cl = ParseDecimalVolume(input_buf);
                if (volume_cl > 0) {
                    // Рассчитываем динамический лимит объёма на основе цены
                    uint32_t max_volume_by_price = (999900 * 100) / global_price;  // 999900 коп / цена в коп
                    uint32_t max_volume_limit = (max_volume_by_price < 90000) ? max_volume_by_price : 90000;
                    
                    if (volume_cl <= max_volume_limit) {
                        target_volume_cl = volume_cl;
                        target_amount = 0;
                        transaction_closed = 0;
                        fuelling_entry_tick = HAL_GetTick();
                        Dispenser_StartVolume(1, volume_cl, global_price);
                        ui_state = UI_STATE_FUELLING;
                    } else {
                        UsbLog_Printf("ERROR: Volume %u.%02u L exceeds max %u.%02u L (price %u.%02u)\r\n", 
                            (unsigned int)(volume_cl/100), (unsigned int)(volume_cl%100),
                            (unsigned int)(max_volume_limit/100), (unsigned int)(max_volume_limit%100),
                            (unsigned int)(global_price/100), (unsigned int)(global_price%100));
                        char error_msg[32];
                        snprintf(error_msg, sizeof(error_msg), "Max %u.%02u L", 
                            (unsigned int)(max_volume_limit/100), (unsigned int)(max_volume_limit%100));
                        ShowErrorMessage(error_msg);
                    }
                }
            }
            else if (key == 'E') {
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            }
            else if (key == 'F') {
                ui_state = UI_STATE_MAIN;
            }
            break;
            
        case UI_STATE_INPUT_AMOUNT:
            if (key >= '0' && key <= '9') {
                if (input_pos < 10) {
                    input_buf[input_pos++] = key;
                    input_buf[input_pos] = '\0';
                }
            } else if (key == 'K') {
                uint32_t amount = atol(input_buf);
                if (amount > 0) {
                    // Рассчитываем динамический лимит суммы на основе цены и максимального объёма
                    uint32_t max_amount_by_volume = (90000 * global_price) / 100;  // 900.00 л * цена в копейках
                    uint32_t max_amount_limit = (max_amount_by_volume < 999900) ? max_amount_by_volume : 999900;
                    
                    if (amount <= max_amount_limit) {
                        target_amount = amount;
                        target_volume_cl = 0;
                        transaction_closed = 0;
                        fuelling_entry_tick = HAL_GetTick();
                        Dispenser_StartAmount(1, amount, global_price);
                        ui_state = UI_STATE_FUELLING;
                    } else {
                        UsbLog_Printf("ERROR: Amount %lu exceeds max %lu (price %u.%02u, max vol %u.%02u L)\r\n", 
                            (unsigned long)amount, (unsigned long)max_amount_limit,
                            (unsigned int)(global_price/100), (unsigned int)(global_price%100),
                            (unsigned int)(90000/100), (unsigned int)(90000%100));
                        char error_msg[32];
                        snprintf(error_msg, sizeof(error_msg), "Max %lu", (unsigned long)max_amount_limit);
                        ShowErrorMessage(error_msg);
                    }
                }
            } else if (key == 'E') {
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            } else if (key == 'F') {
                ui_state = UI_STATE_MAIN;
            }
            break;
            
        case UI_STATE_FUELLING:
            if ((HAL_GetTick() - fuelling_entry_tick) > FUELLING_TIMEOUT_MS) {
                UsbLog_Printf("Fuelling timeout\r\n");
                if (!transaction_closed && (g_dispenser.volume_cl > 0 || g_dispenser.amount > 0)) {
                    Dispenser_CloseTransaction();
                    transaction_closed = 1;
                    transaction_end_tick = HAL_GetTick();
                }
                ui_state = UI_STATE_TRANSACTION_RESULT;
                break;
            }
            
            if (g_dispenser.status == DS_IDLE || g_dispenser.status == DS_CALLING) {
                if (g_dispenser.volume_cl > 0 || g_dispenser.amount > 0) {
                    UsbLog_Printf("Dispenser returned to IDLE/CALLING with data\r\n");
                    if (!transaction_closed) {
                        Dispenser_CloseTransaction();
                        transaction_closed = 1;
                        transaction_end_tick = HAL_GetTick();
                    }
                    ui_state = UI_STATE_TRANSACTION_RESULT;
                    break;
                }
            }
            else if (g_dispenser.status == DS_END) {
                if (!transaction_closed) {
                    UsbLog_Printf("Transaction END detected\r\n");
                    Dispenser_CloseTransaction();
                    transaction_closed = 1;
                    transaction_end_tick = HAL_GetTick();
                }
                ui_state = UI_STATE_TRANSACTION_RESULT;
                break;
            }
            
            if (key == 'F') {
                UsbLog_Printf("Manual exit from fuelling\r\n");
                ui_state = UI_STATE_MAIN;
                transaction_closed = 0;
            }
            break;
            
        case UI_STATE_TRANSACTION_RESULT:
            if ((HAL_GetTick() - transaction_end_tick) > 30000) {
                ui_state = UI_STATE_MAIN;
                transaction_closed = 0;
                break;
            }
            
            if (key == 'F') {
                ui_state = prev_transaction_mode;
                input_pos = 0;
                memset(input_buf, 0, sizeof(input_buf));
            } else if (key == 'E') {
                ui_state = UI_STATE_MAIN;
            }
            break;
            
        case UI_STATE_ERROR_MESSAGE:
            // При любом нажатии клавиши возвращаемся в главное меню
            if (key != 0) {
                ui_state = UI_STATE_MAIN;
            }
            break;
    }
}

void UI_Draw(void) {
    uint32_t now = HAL_GetTick();
    
    if (now - last_ui_draw_tick < 33) { 
        return;
    }
    last_ui_draw_tick = now;

    switch (ui_state) {
        case UI_STATE_MAIN:
            DrawMain();
            break;
        case UI_STATE_TOTALIZER:
            DrawTotalizer();
            break;
        case UI_STATE_SET_PRICE:
            DrawSetPrice();
            break;
        case UI_STATE_INPUT_VOLUME:
            DrawInputVolume();
            break;
        case UI_STATE_INPUT_AMOUNT:
            DrawInputAmount();
            break;
        case UI_STATE_FUELLING:
            DrawFuelling();
            break;
        case UI_STATE_TRANSACTION_RESULT:
            DrawTransactionResult();
            break;
        case UI_STATE_ERROR_MESSAGE:
            DrawErrorMessage();
            // Автоматический возврат после таймаута или при нажатии любой клавиши
            if ((now - error_display_start) > error_display_duration) {
                ui_state = UI_STATE_MAIN;
            }
            break;
    }
}
