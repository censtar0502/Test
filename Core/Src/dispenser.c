#include "dispenser.h"
#include "usart.h"
#include "gaskitlink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void UsbLog_Printf(const char *fmt, ...);

Dispenser_t g_dispenser;

// Буферы для каждого UART (для каждого ведомого)
static uint8_t tx_buf[64];

// Буферы для USART2 (ведущий 1)
static uint8_t rx_dma_buf_uart2[64];
static uint8_t rx_frame_buf_uart2[64];
static volatile uint16_t rx_len_uart2 = 0;
static volatile uint8_t rx_ready_uart2 = 0;

// Буферы для USART3 (ведущий 2)
static uint8_t rx_dma_buf_uart3[64];
static uint8_t rx_frame_buf_uart3[64];
static volatile uint16_t rx_len_uart3 = 0;
static volatile uint8_t rx_ready_uart3 = 0;

// Таймауты для машины состояний (мс)
#define STATE_TIMEOUT_SHORT     100
#define STATE_TIMEOUT_IDLE      500
#define STATE_TIMEOUT_FUELLING  200
#define STATE_TIMEOUT_N         3000

// Счётчик попыток для повторных отправок
static uint8_t retry_counts[2] = {0, 0};  // Для каждого ведомого
#define MAX_RETRIES 3

void Dispenser_Init(void) {
    memset(&g_dispenser, 0, sizeof(Dispenser_t));
    
    // Инициализация первого ведомого (USART2, адрес 0x01)
    g_dispenser.units[0].status = DS_IDLE;
    g_dispenser.units[0].state = STATE_IDLE;
    g_dispenser.units[0].state_entry_tick = HAL_GetTick();
    g_dispenser.units[0].huart = &huart2;
    g_dispenser.units[0].slave_address = 0x01;
    g_dispenser.units[0].t_command_sent = 0;
    g_dispenser.units[0].transaction_closed = 0;
    
    // Инициализация второго ведомого (USART3, адрес 0x02)
    g_dispenser.units[1].status = DS_IDLE;
    g_dispenser.units[1].state = STATE_IDLE;
    g_dispenser.units[1].state_entry_tick = HAL_GetTick();
    g_dispenser.units[1].huart = &huart3;
    g_dispenser.units[1].slave_address = 0x02;
    g_dispenser.units[1].t_command_sent = 0;
    g_dispenser.units[1].transaction_closed = 0;
    
    // Установка активного ведомого по умолчанию (первый)
    g_dispenser.active_unit = 0;
    
    // Запуск приема данных для обоих UART
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf_uart2, sizeof(rx_dma_buf_uart2));
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buf_uart3, sizeof(rx_dma_buf_uart3));
}

static void ChangeState(uint8_t unit_idx, DispenserState_t new_state) {
    if (unit_idx >= 2) return;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    
    if (unit->state != new_state) {
        unit->state = new_state;
        unit->state_entry_tick = HAL_GetTick();
        retry_counts[unit_idx] = 0;

        const char* state_names[] = {
            "IDLE", "SEND_STATUS", "WAIT_STATUS", "SEND_L", "WAIT_L",
            "SEND_R", "WAIT_R", "SEND_T", "WAIT_T", "SEND_N", "WAIT_N", "ERROR"
        };
        if (new_state < sizeof(state_names) / sizeof(state_names[0])) {
            UsbLog_Printf("[UNIT%d] [STATE] -> %s\r\n", unit_idx + 1, state_names[new_state]);
        }
    }
}

static void SendFrame(uint8_t unit_idx, char cmd, const char *data) {
    if (unit_idx >= 2) return;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    uint8_t addr_high = 0x00;
    uint8_t addr_low = unit->slave_address;  // 0x01 для первого, 0x02 для второго
    
    uint16_t len = Gas_BuildFrame(tx_buf, addr_high, addr_low, cmd, data);
    
    HAL_StatusTypeDef status = HAL_UART_Transmit(unit->huart, tx_buf, len, 100);
    if (status != HAL_OK) {
        UsbLog_Printf("TX ERROR UNIT%d: cmd=%c, status=%d\r\n", unit_idx + 1, cmd, status);
    }

    if (data && data[0] != '\0') {
        UsbLog_Printf("UNIT%d TX: %c%s\r\n", unit_idx + 1, cmd, data);
    } else {
        UsbLog_Printf("UNIT%d TX: %c\r\n", unit_idx + 1, cmd);
    }
}

static uint8_t IsStateTimeout(uint8_t unit_idx, uint32_t timeout_ms) {
    if (unit_idx >= 2) return 0;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    uint32_t now = HAL_GetTick();
    return (now - unit->state_entry_tick) > timeout_ms;
}

static uint8_t ProcessStatusResponse(uint8_t unit_idx, const GasFrame_t *frame) {
    if (unit_idx >= 2) return 0;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    
    if (frame->data_len < 2) return 0;
    
    char nozzle_char = frame->data[0];
    char status_char = frame->data[1];
    
    unit->nozzle = nozzle_char - '0';
    unit->is_connected = 1;
    unit->last_update_tick = HAL_GetTick();

    UsbLog_Printf("UNIT%d RX: S[nozzle=%c][status=%c] len=%d\r\n",
        unit_idx + 1, nozzle_char, status_char, frame->data_len);

    if (nozzle_char == '9' && status_char == '0') {
        unit->status = DS_END;
        UsbLog_Printf("UNIT%d S90 - Transaction end, nozzle hung\r\n", unit_idx + 1);
        return 1;
    }
    else if (nozzle_char == '1' && status_char == '0') {
        unit->status = DS_IDLE;
        return 0;
    }
    else if (nozzle_char == '2' && status_char == '1') {
        unit->status = DS_CALLING;
        UsbLog_Printf("UNIT%d S21 - Nozzle lifted without authorization\r\n", unit_idx + 1);
        return 0;
    }
    else if (nozzle_char == '3' && status_char == '1') {
        unit->status = DS_AUTHORIZED;
        if (unit->volume_cl > 0 || unit->amount > 0) {
            UsbLog_Printf("UNIT%d New transaction authorized - resetting previous data\r\n", unit_idx + 1);
            unit->volume_cl = 0;
            unit->amount = 0;
            unit->transaction_id = 0;
        }
        UsbLog_Printf("UNIT%d S31 - Transaction authorized\r\n", unit_idx + 1);
        return 0;
    }
    else if (nozzle_char == '4' && status_char == '1') {
        unit->status = DS_STARTED;
        UsbLog_Printf("UNIT%d S41 - Transaction started\r\n", unit_idx + 1);
        return 0;
    }
    else if (nozzle_char == '6' && status_char == '1') {
        unit->status = DS_FUELLING;
        UsbLog_Printf("UNIT%d S61 - Fuelling in progress\r\n", unit_idx + 1);
        return 1;
    }
    else if (nozzle_char == '8' && status_char == '1') {
        unit->status = DS_STOP;
        UsbLog_Printf("UNIT%d S81 - Transaction completed, nozzle not hung\r\n", unit_idx + 1);
        return 1;
    }
    else {
        UsbLog_Printf("UNIT%d Unknown status S%c%c\r\n", unit_idx + 1, nozzle_char, status_char);
        unit->status = DS_IDLE;
        return 0;
    }
}

static void ProcessUnitUpdate(uint8_t unit_idx) {
    if (unit_idx >= 2) return;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    UART_HandleTypeDef *huart = unit->huart;
    
    uint8_t *rx_dma_buf = (unit_idx == 0) ? rx_dma_buf_uart2 : rx_dma_buf_uart3;
    uint8_t *rx_frame_buf = (unit_idx == 0) ? rx_frame_buf_uart2 : rx_frame_buf_uart3;
    volatile uint16_t *rx_len = (unit_idx == 0) ? &rx_len_uart2 : &rx_len_uart3;
    volatile uint8_t *rx_ready = (unit_idx == 0) ? &rx_ready_uart2 : &rx_ready_uart3;
    
    uint32_t now = HAL_GetTick();
    
    uint8_t local_rx_buf[64];
    uint16_t local_rx_len = 0;
    uint8_t has_data = 0;
    
    // Атомарное копирование данных из ISR
    __disable_irq();
    if (*rx_ready) {
        local_rx_len = *rx_len;
        memcpy(local_rx_buf, rx_frame_buf, local_rx_len);
        *rx_ready = 0;
        has_data = 1;
    }
    __enable_irq();

    switch (unit->state) {

        case STATE_IDLE:
            {
                uint32_t timeout = (unit->status == DS_FUELLING ||
                                   unit->status == DS_STARTED ||
                                   unit->status == DS_STOP)
                                   ? STATE_TIMEOUT_FUELLING
                                   : STATE_TIMEOUT_IDLE;

                if (IsStateTimeout(unit_idx, timeout)) {
                    ChangeState(unit_idx, STATE_SEND_STATUS);
                }
            }
            break;

        case STATE_SEND_STATUS:
            SendFrame(unit_idx, 'S', "");
            ChangeState(unit_idx, STATE_WAIT_STATUS);
            break;

        case STATE_WAIT_STATUS:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'S') {
                    uint8_t needs_action = ProcessStatusResponse(unit_idx, &frame);

                    if (needs_action) {
                        if (unit->status == DS_FUELLING) {
                            // Continue S-L-R-S cycle during fuelling
                            ChangeState(unit_idx, STATE_SEND_L);
                        }
                        else if (unit->status == DS_STOP) {
                            if (!unit->t_command_sent) {
                                UsbLog_Printf("UNIT%d S81 - Sending T command (first time)\r\n", unit_idx + 1);
                                unit->t_command_sent = 1;
                                ChangeState(unit_idx, STATE_SEND_T);
                            } else {
                                UsbLog_Printf("UNIT%d S81 - T already sent, waiting for S90\r\n", unit_idx + 1);
                                ChangeState(unit_idx, STATE_IDLE);
                            }
                        }
                        else if (unit->status == DS_END) {
                            unit->t_command_sent = 0;
                            unit->transaction_closed = 1;
                            ChangeState(unit_idx, STATE_SEND_N);
                        }
                        else {
                            ChangeState(unit_idx, STATE_IDLE);
                        }
                    } else {
                        ChangeState(unit_idx, STATE_IDLE);
                    }

                    // DMA перезапускается в прерывании UARTEx_RxEventCallback
                }
                has_data = 0;
            }
            else if (IsStateTimeout(unit_idx, STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] UNIT%d WAIT_STATUS\r\n", unit_idx + 1);
                if (retry_counts[unit_idx] < MAX_RETRIES) {
                    retry_counts[unit_idx]++;
                    ChangeState(unit_idx, STATE_SEND_STATUS);
                } else {
                    ChangeState(unit_idx, STATE_ERROR);
                }
            }
            break;

        case STATE_SEND_L:
            SendFrame(unit_idx, 'L', "");
            ChangeState(unit_idx, STATE_WAIT_L);
            break;

        case STATE_WAIT_L:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'L') {
                    if (frame.data_len >= 10) {
                        unit->nozzle = frame.data[0] - '0';
                        unit->transaction_id = frame.data[1];

                        char volume_str[7] = {0};
                        memcpy(volume_str, &frame.data[4], 6);
                        unit->volume_cl = (uint32_t)atol(volume_str);

                        UsbLog_Printf("UNIT%d L: nozzle=%d, tid='%c', volume=%lu cl\r\n",
                            unit_idx + 1, unit->nozzle, unit->transaction_id,
                            unit->volume_cl);
                    }

                    ChangeState(unit_idx, STATE_SEND_R);
                    // DMA перезапускается в прерывании UARTEx_RxEventCallback
                }
                has_data = 0;
            }
            else if (IsStateTimeout(unit_idx, STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] UNIT%d WAIT_L\r\n", unit_idx + 1);
                if (retry_counts[unit_idx] < MAX_RETRIES) {
                    retry_counts[unit_idx]++;
                    ChangeState(unit_idx, STATE_SEND_L);
                } else {
                    ChangeState(unit_idx, STATE_IDLE);
                }
            }
            break;

        case STATE_SEND_R:
            SendFrame(unit_idx, 'R', "");
            ChangeState(unit_idx, STATE_WAIT_R);
            break;

        case STATE_WAIT_R:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'R') {
                    if (frame.data_len >= 10) {
                        char amount_str[7] = {0};
                        memcpy(amount_str, &frame.data[4], 6);
                        unit->amount = (uint32_t)atol(amount_str);

                        UsbLog_Printf("UNIT%d R: amount=%lu\r\n", unit_idx + 1, unit->amount);
                    }

                    // After R response, continue S-L-R-S cycle if still fuelling
                    if (unit->status == DS_FUELLING) {
                        ChangeState(unit_idx, STATE_SEND_STATUS);  // Continue cycle
                    } else {
                        ChangeState(unit_idx, STATE_IDLE);
                    }
                    // DMA перезапускается в прерывании UARTEx_RxEventCallback
                }
                has_data = 0;
            }
            else if (IsStateTimeout(unit_idx, STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] UNIT%d WAIT_R\r\n", unit_idx + 1);
                if (retry_counts[unit_idx] < MAX_RETRIES) {
                    retry_counts[unit_idx]++;
                    ChangeState(unit_idx, STATE_SEND_R);
                } else {
                    ChangeState(unit_idx, STATE_IDLE);
                }
            }
            break;

        case STATE_SEND_T:
            SendFrame(unit_idx, 'T', "");
            ChangeState(unit_idx, STATE_WAIT_T);
            break;

        case STATE_WAIT_T:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'T') {
                    if (frame.data_len >= 22) {
                        unit->nozzle = frame.data[0] - '0';
                        unit->transaction_id = frame.data[1];

                        char amount_str[7] = {0};
                        memcpy(amount_str, &frame.data[4], 6);
                        unit->amount = (uint32_t)atol(amount_str);

                        char volume_str[7] = {0};
                        memcpy(volume_str, &frame.data[11], 6);
                        unit->volume_cl = (uint32_t)atol(volume_str);

                        UsbLog_Printf("UNIT%d T: nozzle=%d, tid='%c', amount=%lu, volume=%lu cl\r\n",
                            unit_idx + 1, unit->nozzle, unit->transaction_id,
                            unit->amount, unit->volume_cl);
                    }

                    UsbLog_Printf("UNIT%d T received - flag remains set until transaction closes\r\n", unit_idx + 1);
                    ChangeState(unit_idx, STATE_IDLE);
                    // DMA перезапускается в прерывании UARTEx_RxEventCallback
                }
                has_data = 0;
            }
            else if (IsStateTimeout(unit_idx, STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] UNIT%d WAIT_T\r\n", unit_idx + 1);
                ChangeState(unit_idx, STATE_IDLE);
            }
            break;

        case STATE_SEND_N:
            SendFrame(unit_idx, 'N', "");
            ChangeState(unit_idx, STATE_WAIT_N);
            break;

        case STATE_WAIT_N:
            if (has_data) {
                // Обрабатываем ответ на команду N (обычно пустой ответ)
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0) {
                    UsbLog_Printf("UNIT%d N response received\r\n", unit_idx + 1);
                    // Сбрасываем флаги и возвращаемся к опросу статуса
                    unit->t_command_sent = 0;
                    unit->transaction_closed = 0;
                    ChangeState(unit_idx, STATE_SEND_STATUS);
                    // DMA перезапускается в прерывании UARTEx_RxEventCallback
                }
                has_data = 0;
            }
            else if (IsStateTimeout(unit_idx, 200)) {
                ChangeState(unit_idx, STATE_SEND_STATUS);
            }
            break;

        case STATE_ERROR:
            UsbLog_Printf("UNIT%d [ERROR] Communication error, resetting\r\n", unit_idx + 1);
            unit->is_connected = 0;
            unit->t_command_sent = 0;
            if (IsStateTimeout(unit_idx, 500)) {
                UsbLog_Printf("UNIT%d [ERROR] Recovery, returning to IDLE\r\n", unit_idx + 1);
                ChangeState(unit_idx, STATE_IDLE);
            }
            break;

        default:
            ChangeState(unit_idx, STATE_IDLE);
            break;
    }

    // Проверка внеочередных сообщений
    __disable_irq();
    if (*rx_ready && !has_data) {
        local_rx_len = *rx_len;
        memcpy(local_rx_buf, rx_frame_buf, local_rx_len);
        *rx_ready = 0;
        has_data = 1;
    }
    __enable_irq();
    
    if (has_data) {
        GasFrame_t frame;
        if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0) {
            if (frame.cmd == 'C' && frame.data_len >= 11) {
                char totalizer_str[10] = {0};
                memcpy(totalizer_str, &frame.data[2], 9);
                unit->totalizer = (uint64_t)atoll(totalizer_str);

                UsbLog_Printf("UNIT%d C: nozzle=%c, totalizer=%llu\r\n",
                    unit_idx + 1, frame.data[0], (unsigned long long)unit->totalizer);
                
                // Обновляем цену для этого ведомого, если получили общий тотализатор
                if (frame.data[0] == '0') {
                    // Цена хранится в другом месте, но это пример обработки
                }
            }
            else if (frame.cmd != 'S' && frame.cmd != 'L' && frame.cmd != 'R' && frame.cmd != 'T') {
                UsbLog_Printf("UNIT%d RX unexpected cmd '%c': %.*s\r\n",
                    unit_idx + 1, frame.cmd, frame.data_len, frame.data);
            }
        }
        HAL_UARTEx_ReceiveToIdle_DMA(huart, rx_dma_buf, sizeof(rx_dma_buf));
    }
    
    if (now - unit->last_update_tick > 2000) {
        if (unit->is_connected) {
            unit->is_connected = 0;
            UsbLog_Printf("UNIT%d Dispenser connection timeout!\r\n", unit_idx + 1);
            ChangeState(unit_idx, STATE_ERROR);
        }
    }
}

void Dispenser_Update(void) {
    // Обновление обоих ведомых устройств
    ProcessUnitUpdate(0);  // Ведомый 1 (USART2)
    ProcessUnitUpdate(1);  // Ведомый 2 (USART3)
}

void Dispenser_StartVolume(uint8_t unit_idx, uint8_t nozzle, uint32_t volume_cl, uint32_t price) {
    if (unit_idx >= 2) return;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    
    unit->volume_cl = 0;
    unit->amount = 0;
    unit->transaction_id = 0;
    unit->t_command_sent = 0;
    unit->transaction_closed = 0;

    UsbLog_Printf("UNIT%d Starting new volume transaction - reset T flag\r\n", unit_idx + 1);

    char data[32];
    snprintf(data, sizeof(data), "%d;%06lu;%04lu",
        (int)nozzle, (unsigned long)volume_cl, (unsigned long)price);
    SendFrame(unit_idx, 'V', data);
}

void Dispenser_StartAmount(uint8_t unit_idx, uint8_t nozzle, uint32_t amount, uint32_t price) {
    if (unit_idx >= 2) return;
    
    DispenserUnit_t *unit = &g_dispenser.units[unit_idx];
    
    unit->volume_cl = 0;
    unit->amount = 0;
    unit->transaction_id = 0;
    unit->t_command_sent = 0;
    unit->transaction_closed = 0;

    UsbLog_Printf("UNIT%d Starting new amount transaction - reset T flag\r\n", unit_idx + 1);

    char data[32];
    snprintf(data, sizeof(data), "%d;%06lu;%04lu",
        (int)nozzle, (unsigned long)amount, (unsigned long)price);
    SendFrame(unit_idx, 'M', data);
}

void Dispenser_RequestTotalizer(uint8_t unit_idx) {
    if (unit_idx >= 2) return;
    
    SendFrame(unit_idx, 'C', "0");
}

void Dispenser_Stop(uint8_t unit_idx) {
    if (unit_idx >= 2) return;
    
    SendFrame(unit_idx, 'B', "");
}

void Dispenser_Resume(uint8_t unit_idx) {
    if (unit_idx >= 2) return;
    
    SendFrame(unit_idx, 'G', "");
}

void Dispenser_CloseTransaction(uint8_t unit_idx) {
    if (unit_idx >= 2) return;
    
    SendFrame(unit_idx, 'N', "");
}

// Функции для переключения между ведомыми
void Dispenser_SwitchActiveUnit(uint8_t unit_idx) {
    if (unit_idx < 2) {
        g_dispenser.active_unit = unit_idx;
    }
}

uint8_t Dispenser_GetActiveUnit(void) {
    return g_dispenser.active_unit;
}

DispenserUnit_t* Dispenser_GetUnit(uint8_t unit_idx) {
    if (unit_idx < 2) {
        return &g_dispenser.units[unit_idx];
    }
    return NULL;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart == &huart2) {
        if (Size <= sizeof(rx_frame_buf_uart2)) {
            memcpy(rx_frame_buf_uart2, rx_dma_buf_uart2, Size);
            rx_len_uart2 = Size;
            rx_ready_uart2 = 1;

            #if 1
            UsbLog_Printf("UART2 RAW RX (%d): ", Size);
            for (uint16_t i = 0; i < Size; i++) {
                UsbLog_Printf("%02X ", rx_dma_buf_uart2[i]);
            }
            UsbLog_Printf(" | ");
            for (uint16_t i = 0; i < Size; i++) {
                if (rx_dma_buf_uart2[i] >= 32 && rx_dma_buf_uart2[i] < 127) {
                    UsbLog_Printf("%c", rx_dma_buf_uart2[i]);
                } else {
                    UsbLog_Printf(".");
                }
            }
            UsbLog_Printf("\r\n");
            #endif
        }
        // ВАЖНО: Перезапуск DMA для продолжения приема
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf_uart2, sizeof(rx_dma_buf_uart2));
    }
    else if (huart == &huart3) {
        if (Size <= sizeof(rx_frame_buf_uart3)) {
            memcpy(rx_frame_buf_uart3, rx_dma_buf_uart3, Size);
            rx_len_uart3 = Size;
            rx_ready_uart3 = 1;

            #if 1
            UsbLog_Printf("UART3 RAW RX (%d): ", Size);
            for (uint16_t i = 0; i < Size; i++) {
                UsbLog_Printf("%02X ", rx_dma_buf_uart3[i]);
            }
            UsbLog_Printf(" | ");
            for (uint16_t i = 0; i < Size; i++) {
                if (rx_dma_buf_uart3[i] >= 32 && rx_dma_buf_uart3[i] < 127) {
                    UsbLog_Printf("%c", rx_dma_buf_uart3[i]);
                } else {
                    UsbLog_Printf(".");
                }
            }
            UsbLog_Printf("\r\n");
            #endif
        }
        // ВАЖНО: Перезапуск DMA для продолжения приема
        HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buf_uart3, sizeof(rx_dma_buf_uart3));
    }
}