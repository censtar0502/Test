#ifndef DISPENSER_H
#define DISPENSER_H

#include "main.h"
#include "gaskitlink.h"

typedef enum {
    DS_IDLE = 0,              // S10
    DS_CALLING,               // S21
    DS_AUTHORIZED,            // S31
    DS_STARTED,               // S41
    DS_SUSPENDED_STARTED,     // S5x (если есть)
    DS_FUELLING,              // S61
    DS_SUSPENDED_FUELLING,    // S7x (если есть)
    DS_STOP,                  // S81
    DS_END                    // S90
} DispenserStatus_t;

// Состояния машины состояний для управления последовательностью команд
typedef enum {
    STATE_IDLE,               // Начальное состояние
    STATE_SEND_STATUS,        // Отправка команды S
    STATE_WAIT_STATUS,        // Ожидание ответа на S
    STATE_SEND_L,             // Отправка команды L
    STATE_WAIT_L,             // Ожидание ответа на L
    STATE_SEND_R,             // Отправка команды R
    STATE_WAIT_R,             // Ожидание ответа на R
    STATE_SEND_T,             // Отправка команды T (финальные данные)
    STATE_WAIT_T,             // Ожидание ответа на T
    STATE_SEND_N,             // Отправка команды N (закрытие транзакции)
    STATE_WAIT_N,             // Ожидание подтверждения N
    STATE_ERROR               // Состояние ошибки (таймаут)
} DispenserState_t;

typedef struct {
    DispenserStatus_t status;
    uint8_t nozzle;
    uint32_t volume_cl; // Centiliters
    uint32_t amount;    // Money
    uint32_t price;     // Money per liter
    uint64_t totalizer; // Totalizer in centiliters (9 digits, need 64-bit)
    char transaction_id;
    
    uint8_t is_connected;
    uint32_t last_update_tick;

    // Машина состояний
    DispenserState_t state;
    uint32_t state_entry_tick;  // Время входа в текущее состояние
} Dispenser_t;

void Dispenser_Init(void);
void Dispenser_Update(void);

// Commands
void Dispenser_StartVolume(uint8_t nozzle, uint32_t volume_cl, uint32_t price);
void Dispenser_StartAmount(uint8_t nozzle, uint32_t amount, uint32_t price);
void Dispenser_Stop(void);
void Dispenser_Resume(void);
void Dispenser_CloseTransaction(void);
void Dispenser_RequestTotalizer(void);

extern Dispenser_t g_dispenser;

#endif // DISPENSER_H
