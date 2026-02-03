#include "dispenser.h"
#include "usart.h"
#include "gaskitlink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void UsbLog_Printf(const char *fmt, ...);

Dispenser_t g_dispenser;

static uint8_t tx_buf[64];
static uint8_t rx_dma_buf[64];
static uint8_t rx_frame_buf[64];
static volatile uint16_t rx_len = 0;
static volatile uint8_t rx_ready = 0;

// Таймауты для машины состояний (мс)
#define STATE_TIMEOUT_SHORT     100
#define STATE_TIMEOUT_IDLE      500
#define STATE_TIMEOUT_FUELLING  200
#define STATE_TIMEOUT_N         3000

// Счётчик попыток для повторных отправок
static uint8_t retry_count = 0;
#define MAX_RETRIES 3

// ИСПРАВЛЕНИЕ: Флаг команды T
static uint8_t t_command_sent = 0;

void Dispenser_Init(void) {
    memset(&g_dispenser, 0, sizeof(Dispenser_t));
    g_dispenser.status = DS_IDLE;
    g_dispenser.state = STATE_IDLE;
    g_dispenser.state_entry_tick = HAL_GetTick();
    
    t_command_sent = 0;
    
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
}

static void ChangeState(DispenserState_t new_state) {
    if (g_dispenser.state != new_state) {
        g_dispenser.state = new_state;
        g_dispenser.state_entry_tick = HAL_GetTick();
        retry_count = 0;

        const char* state_names[] = {
            "IDLE", "SEND_STATUS", "WAIT_STATUS", "SEND_L", "WAIT_L",
            "SEND_R", "WAIT_R", "SEND_T", "WAIT_T", "SEND_N", "WAIT_N", "ERROR"
        };
        if (new_state < sizeof(state_names) / sizeof(state_names[0])) {
            UsbLog_Printf("[STATE] -> %s\r\n", state_names[new_state]);
        }
    }
}

static void SendFrame(char cmd, const char *data) {
    uint16_t len = Gas_BuildFrame(tx_buf, 0x00, 0x01, cmd, data);
    
    // ИСПРАВЛЕНИЕ: Проверка результата передачи
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart2, tx_buf, len, 100);
    if (status != HAL_OK) {
        UsbLog_Printf("TX ERROR: cmd=%c, status=%d\r\n", cmd, status);
    }

    if (data && data[0] != '\0') {
        UsbLog_Printf("TX: %c%s\r\n", cmd, data);
    } else {
        UsbLog_Printf("TX: %c\r\n", cmd);
    }
}

static uint8_t IsStateTimeout(uint32_t timeout_ms) {
    uint32_t now = HAL_GetTick();
    return (now - g_dispenser.state_entry_tick) > timeout_ms;
}

static uint8_t ProcessStatusResponse(const GasFrame_t *frame) {
    if (frame->data_len < 2) return 0;
    
    char nozzle_char = frame->data[0];
    char status_char = frame->data[1];
    
    g_dispenser.nozzle = nozzle_char - '0';
    g_dispenser.is_connected = 1;
    g_dispenser.last_update_tick = HAL_GetTick();

    UsbLog_Printf("RX: S[nozzle=%c][status=%c] len=%d\r\n",
        nozzle_char, status_char, frame->data_len);

    if (nozzle_char == '9' && status_char == '0') {
        g_dispenser.status = DS_END;
        UsbLog_Printf("S90 - Transaction end, nozzle hung\r\n");
        return 1;
    }
    else if (nozzle_char == '1' && status_char == '0') {
        g_dispenser.status = DS_IDLE;
        return 0;
    }
    else if (nozzle_char == '2' && status_char == '1') {
        g_dispenser.status = DS_CALLING;
        UsbLog_Printf("S21 - Nozzle lifted without authorization\r\n");
        return 0;
    }
    else if (nozzle_char == '3' && status_char == '1') {
        g_dispenser.status = DS_AUTHORIZED;
        if (g_dispenser.volume_cl > 0 || g_dispenser.amount > 0) {
            UsbLog_Printf("New transaction authorized - resetting previous data\r\n");
            g_dispenser.volume_cl = 0;
            g_dispenser.amount = 0;
            g_dispenser.transaction_id = 0;
        }
        UsbLog_Printf("S31 - Transaction authorized\r\n");
        return 0;
    }
    else if (nozzle_char == '4' && status_char == '1') {
        g_dispenser.status = DS_STARTED;
        UsbLog_Printf("S41 - Transaction started\r\n");
        return 0;
    }
    else if (nozzle_char == '6' && status_char == '1') {
        g_dispenser.status = DS_FUELLING;
        UsbLog_Printf("S61 - Fuelling in progress\r\n");
        return 1;
    }
    else if (nozzle_char == '8' && status_char == '1') {
        g_dispenser.status = DS_STOP;
        UsbLog_Printf("S81 - Transaction completed, nozzle not hung\r\n");
        return 1;
    }
    else {
        UsbLog_Printf("Unknown status S%c%c\r\n", nozzle_char, status_char);
        g_dispenser.status = DS_IDLE;
        return 0;
    }
}

void Dispenser_Update(void) {
    uint32_t now = HAL_GetTick();
    
    // ===================================================================
    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ #1: Атомарное копирование RX данных
    // ===================================================================
    uint8_t local_rx_buf[64];
    uint16_t local_rx_len = 0;
    uint8_t has_data = 0;
    
    // АТОМАРНОЕ КОПИРОВАНИЕ ДАННЫХ ИЗ ISR БУФЕРА
    __disable_irq();
    if (rx_ready) {
        local_rx_len = rx_len;
        memcpy(local_rx_buf, rx_frame_buf, local_rx_len);
        rx_ready = 0;
        has_data = 1;
    }
    __enable_irq();

    switch (g_dispenser.state) {

        case STATE_IDLE:
            {
                uint32_t timeout = (g_dispenser.status == DS_FUELLING ||
                                   g_dispenser.status == DS_STARTED ||
                                   g_dispenser.status == DS_STOP)
                                   ? STATE_TIMEOUT_FUELLING
                                   : STATE_TIMEOUT_IDLE;

                if (IsStateTimeout(timeout)) {
                    ChangeState(STATE_SEND_STATUS);
                }
            }
            break;

        case STATE_SEND_STATUS:
            SendFrame('S', "");
            ChangeState(STATE_WAIT_STATUS);
            break;

        case STATE_WAIT_STATUS:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'S') {
                    uint8_t needs_action = ProcessStatusResponse(&frame);

                    if (needs_action) {
                        if (g_dispenser.status == DS_FUELLING) {
                            ChangeState(STATE_SEND_L);
                        }
                        else if (g_dispenser.status == DS_STOP) {
                            if (!t_command_sent) {
                                UsbLog_Printf("S81 - Sending T command (first time)\r\n");
                                t_command_sent = 1;
                                ChangeState(STATE_SEND_T);
                            } else {
                                UsbLog_Printf("S81 - T already sent, waiting for S90\r\n");
                                ChangeState(STATE_IDLE);
                            }
                        }
                        else if (g_dispenser.status == DS_END) {
                            t_command_sent = 0;
                            ChangeState(STATE_SEND_N);
                        }
                        else {
                            ChangeState(STATE_IDLE);
                        }
                    } else {
                        ChangeState(STATE_IDLE);
                    }

                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            else if (IsStateTimeout(STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] WAIT_STATUS\r\n");
                if (retry_count < MAX_RETRIES) {
                    retry_count++;
                    ChangeState(STATE_SEND_STATUS);
                } else {
                    ChangeState(STATE_ERROR);
                }
            }
            break;

        case STATE_SEND_L:
            SendFrame('L', "");
            ChangeState(STATE_WAIT_L);
            break;

        case STATE_WAIT_L:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'L') {
                    if (frame.data_len >= 10) {
                        g_dispenser.nozzle = frame.data[0] - '0';
                        g_dispenser.transaction_id = frame.data[1];

                        char volume_str[7] = {0};
                        memcpy(volume_str, &frame.data[4], 6);
                        g_dispenser.volume_cl = (uint32_t)atol(volume_str);

                        UsbLog_Printf("L: nozzle=%d, tid='%c', volume=%lu cl\r\n",
                            g_dispenser.nozzle, g_dispenser.transaction_id,
                            g_dispenser.volume_cl);
                    }

                    ChangeState(STATE_SEND_R);
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            else if (IsStateTimeout(STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] WAIT_L\r\n");
                if (retry_count < MAX_RETRIES) {
                    retry_count++;
                    ChangeState(STATE_SEND_L);
                } else {
                    ChangeState(STATE_IDLE);
                }
            }
            break;

        case STATE_SEND_R:
            SendFrame('R', "");
            ChangeState(STATE_WAIT_R);
            break;

        case STATE_WAIT_R:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'R') {
                    if (frame.data_len >= 10) {
                        char amount_str[7] = {0};
                        memcpy(amount_str, &frame.data[4], 6);
                        g_dispenser.amount = (uint32_t)atol(amount_str);

                        UsbLog_Printf("R: amount=%lu\r\n", g_dispenser.amount);
                    }

                    ChangeState(STATE_IDLE);
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            else if (IsStateTimeout(STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] WAIT_R\r\n");
                if (retry_count < MAX_RETRIES) {
                    retry_count++;
                    ChangeState(STATE_SEND_R);
                } else {
                    ChangeState(STATE_IDLE);
                }
            }
            break;

        case STATE_SEND_T:
            SendFrame('T', "");
            ChangeState(STATE_WAIT_T);
            break;

        case STATE_WAIT_T:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'T') {
                    if (frame.data_len >= 22) {
                        g_dispenser.nozzle = frame.data[0] - '0';
                        g_dispenser.transaction_id = frame.data[1];

                        char amount_str[7] = {0};
                        memcpy(amount_str, &frame.data[4], 6);
                        g_dispenser.amount = (uint32_t)atol(amount_str);

                        char volume_str[7] = {0};
                        memcpy(volume_str, &frame.data[11], 6);
                        g_dispenser.volume_cl = (uint32_t)atol(volume_str);

                        UsbLog_Printf("T: nozzle=%d, tid='%c', amount=%lu, volume=%lu cl\r\n",
                            g_dispenser.nozzle, g_dispenser.transaction_id,
                            g_dispenser.amount, g_dispenser.volume_cl);
                    }

                    UsbLog_Printf("T received - flag remains set until transaction closes\r\n");
                    ChangeState(STATE_IDLE);
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            else if (IsStateTimeout(STATE_TIMEOUT_SHORT)) {
                UsbLog_Printf("[TIMEOUT] WAIT_T\r\n");
                ChangeState(STATE_IDLE);
            }
            break;

        case STATE_SEND_N:
            SendFrame('N', "");
            ChangeState(STATE_WAIT_N);
            break;

        case STATE_WAIT_N:
            if (IsStateTimeout(200)) {
                ChangeState(STATE_SEND_STATUS);
            }
            break;

        case STATE_ERROR:
            UsbLog_Printf("[ERROR] Communication error, resetting\r\n");
            g_dispenser.is_connected = 0;
            t_command_sent = 0;  // ИСПРАВЛЕНИЕ: Сброс флага при ошибке
            if (IsStateTimeout(500)) {  // ИСПРАВЛЕНИЕ: Уменьшен до 500мс
                UsbLog_Printf("[ERROR] Recovery, returning to IDLE\r\n");
                ChangeState(STATE_IDLE);
            }
            break;

        default:
            ChangeState(STATE_IDLE);
            break;
    }

    // Проверка внеочередных сообщений
    __disable_irq();
    if (rx_ready && !has_data) {
        local_rx_len = rx_len;
        memcpy(local_rx_buf, rx_frame_buf, local_rx_len);
        rx_ready = 0;
        has_data = 1;
    }
    __enable_irq();
    
    if (has_data) {
        GasFrame_t frame;
        if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0) {
            if (frame.cmd == 'C' && frame.data_len >= 11) {
                char totalizer_str[10] = {0};
                memcpy(totalizer_str, &frame.data[2], 9);
                g_dispenser.totalizer = (uint64_t)atoll(totalizer_str);

                UsbLog_Printf("C: nozzle=%c, totalizer=%llu\r\n",
                    frame.data[0], (unsigned long long)g_dispenser.totalizer);
            }
            else if (frame.cmd != 'S' && frame.cmd != 'L' && frame.cmd != 'R' && frame.cmd != 'T') {
                UsbLog_Printf("RX unexpected cmd '%c': %.*s\r\n",
                    frame.cmd, frame.data_len, frame.data);
            }
        }
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
    }
    
    if (now - g_dispenser.last_update_tick > 2000) {
        if (g_dispenser.is_connected) {
            g_dispenser.is_connected = 0;
            UsbLog_Printf("Dispenser connection timeout!\r\n");
            ChangeState(STATE_ERROR);
        }
    }
}

void Dispenser_StartVolume(uint8_t nozzle, uint32_t volume_cl, uint32_t price) {
    g_dispenser.volume_cl = 0;
    g_dispenser.amount = 0;
    g_dispenser.transaction_id = 0;
    t_command_sent = 0;

    UsbLog_Printf("Starting new volume transaction - reset T flag\r\n");

    char data[32];
    snprintf(data, sizeof(data), "%d;%06lu;%04lu",
        (int)nozzle, (unsigned long)volume_cl, (unsigned long)price);
    SendFrame('V', data);
}

void Dispenser_StartAmount(uint8_t nozzle, uint32_t amount, uint32_t price) {
    g_dispenser.volume_cl = 0;
    g_dispenser.amount = 0;
    g_dispenser.transaction_id = 0;
    t_command_sent = 0;

    UsbLog_Printf("Starting new amount transaction - reset T flag\r\n");

    char data[32];
    snprintf(data, sizeof(data), "%d;%06lu;%04lu",
        (int)nozzle, (unsigned long)amount, (unsigned long)price);
    SendFrame('M', data);
}

void Dispenser_RequestTotalizer(void) {
    SendFrame('C', "0");
}

void Dispenser_Stop(void) {
    SendFrame('B', "");
}

void Dispenser_Resume(void) {
    SendFrame('G', "");
}

void Dispenser_CloseTransaction(void) {
    SendFrame('N', "");
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart == &huart2) {
        if (Size <= sizeof(rx_frame_buf)) {
            memcpy(rx_frame_buf, rx_dma_buf, Size);
            rx_len = Size;
            rx_ready = 1;

            #if 1
            UsbLog_Printf("RAW RX (%d): ", Size);
            for (uint16_t i = 0; i < Size; i++) {
                UsbLog_Printf("%02X ", rx_dma_buf[i]);
            }
            UsbLog_Printf(" | ");
            for (uint16_t i = 0; i < Size; i++) {
                if (rx_dma_buf[i] >= 32 && rx_dma_buf[i] < 127) {
                    UsbLog_Printf("%c", rx_dma_buf[i]);
                } else {
                    UsbLog_Printf(".");
                }
            }
            UsbLog_Printf("\r\n");
            #endif
        }
    }
}
