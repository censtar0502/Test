# ИСПРАВЛЕНИЯ КРИТИЧЕСКИХ БАГОВ

## 🔴 BUG #1: Race Condition в UART RX (ПРИОРИТЕТ 1)

### Проблема:
ISR может перезаписать `rx_frame_buf` пока main loop его читает.

### Решение:

```c
// В dispenser.c

// Изменить обработку rx_ready на атомарную
void Dispenser_Update(void) {
    uint32_t now = HAL_GetTick();
    
    // Локальные копии для атомарной обработки
    uint8_t local_rx_buf[64];
    uint16_t local_rx_len = 0;
    uint8_t has_data = 0;
    
    // ===================================================================
    // АТОМАРНОЕ КОПИРОВАНИЕ ДАННЫХ ИЗ ISR БУФЕРА
    // ===================================================================
    
    __disable_irq();  // Критическая секция начало
    if (rx_ready) {
        local_rx_len = rx_len;
        memcpy(local_rx_buf, rx_frame_buf, local_rx_len);
        rx_ready = 0;
        has_data = 1;
    }
    __enable_irq();  // Критическая секция конец
    
    // ===================================================================
    // МАШИНА СОСТОЯНИЙ (работает с локальной копией)
    // ===================================================================

    switch (g_dispenser.state) {
        
        case STATE_WAIT_STATUS:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'S') {
                    // ... обработка ...
                    
                    // ВАЖНО: Перезапускаем DMA СРАЗУ после обработки
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;  // Пометить как обработанное
            }
            // ... таймауты и прочее ...
            break;
            
        // ... остальные states аналогично ...
        
        case STATE_WAIT_L:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'L') {
                    // ... обработка ...
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            break;
            
        case STATE_WAIT_R:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'R') {
                    // ... обработка ...
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            break;
            
        case STATE_WAIT_T:
            if (has_data) {
                GasFrame_t frame;
                if (Gas_ParseFrame(local_rx_buf, local_rx_len, &frame) == 0 && frame.cmd == 'T') {
                    // ... обработка ...
                    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
                }
                has_data = 0;
            }
            break;
    }

    // ===================================================================
    // ОБРАБОТКА ВНЕОЧЕРЕДНЫХ СООБЩЕНИЙ (C команда и прочее)
    // ===================================================================
    
    // Проверяем ещё раз, возможно пришли новые данные
    __disable_irq();
    if (rx_ready) {
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
                // ... обработка C ...
            }
        }
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, sizeof(rx_dma_buf));
    }
    
    // ... connection timeout check ...
}
```

### Альтернативное решение (Double Buffering):

```c
// В начале файла
static uint8_t rx_dma_buf[2][64];  // Два буфера
static uint8_t active_buf = 0;      // Какой буфер активен
static uint8_t rx_frame_buf[64];   // Рабочий буфер для main

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart == &huart2) {
        if (Size <= sizeof(rx_frame_buf)) {
            // Копируем из активного DMA буфера в рабочий
            memcpy(rx_frame_buf, rx_dma_buf[active_buf], Size);
            rx_len = Size;
            rx_ready = 1;
            
            // Переключаем на другой буфер
            active_buf = 1 - active_buf;
            
            // Перезапускаем DMA на новый буфер
            HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf[active_buf], sizeof(rx_dma_buf[0]));
        }
    }
}
```

---

## 🔴 BUG #2: Переполнение input_buf (ПРИОРИТЕТ 2)

### Решение:

```c
// В ui_manager.c

#define INPUT_BUF_MAX_CHARS 10  // Максимум символов для ввода (не включая '\0')
static char input_buf[INPUT_BUF_MAX_CHARS + 1];  // +1 для '\0'
static uint8_t input_pos = 0;

// Во всех местах ввода:
case UI_STATE_SET_PRICE:
    if (key >= '0' && key <= '9') {
        if (input_pos < INPUT_BUF_MAX_CHARS) {  // ✅ Правильная проверка
            input_buf[input_pos++] = key;
            input_buf[input_pos] = '\0';
        } else {
            // Опционально: звуковой сигнал или сообщение об ошибке
            UsbLog_Printf("Input buffer full!\r\n");
        }
    }
    // ... остальной код ...
    break;

// Аналогично для UI_STATE_INPUT_VOLUME и UI_STATE_INPUT_AMOUNT
```

---

## 🟡 BUG #3: Множественная отправка N (ПРИОРИТЕТ 3)

### Решение:

```c
// В ui_manager.c

case UI_STATE_INPUT_VOLUME:
    if (key == 'K') {
        uint32_t volume = atol(input_buf);
        if (volume > 0 && volume <= 900) {
            target_volume_cl = volume * 100;
            target_amount = 0;
            transaction_closed = 0;  // ✅ Сбрасываем при НАЧАЛЕ транзакции
            fuelling_entry_tick = HAL_GetTick();
            Dispenser_StartVolume(1, volume * 100, global_price);
            ui_state = UI_STATE_FUELLING;
        }
    }
    break;

case UI_STATE_INPUT_AMOUNT:
    if (key == 'K') {
        uint32_t amount = atol(input_buf);
        if (amount > 0) {
            target_amount = amount;
            target_volume_cl = 0;
            transaction_closed = 0;  // ✅ Сбрасываем при НАЧАЛЕ транзакции
            fuelling_entry_tick = HAL_GetTick();
            Dispenser_StartAmount(1, amount, global_price);
            ui_state = UI_STATE_FUELLING;
        }
    }
    break;

case UI_STATE_TRANSACTION_RESULT:
    if (key == 'F') {
        ui_state = prev_transaction_mode;
        input_pos = 0;
        memset(input_buf, 0, sizeof(input_buf));
        // ❌ НЕ сбрасываем transaction_closed здесь!
        // Он сбросится при нажатии K в следующей транзакции
    } else if (key == 'E') {
        ui_state = UI_STATE_MAIN;
        // ❌ НЕ сбрасываем transaction_closed здесь!
    }
    break;
```

---

## 🟡 BUG #5: Избыточная проверка переполнения (ПРИОРИТЕТ 5)

### Решение:

```c
// В keyboard.c

char Keyboard_GetKey(void) {
    uint32_t now = HAL_GetTick();
    
    // Unsigned arithmetic автоматически обрабатывает переполнение!
    if ((now - last_scan_time) >= 50) {  // ✅ Простая проверка
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
```

---

## 🟡 BUG #6: Валидация EEPROM (ПРИОРИТЕТ 6)

### Решение:

```c
// В ui_manager.c

void UI_Init(void) {
    Keyboard_Init();
    Dispenser_Init();
    
    global_price = EEPROM_LoadPrice();
    
    // Строгая валидация с диапазоном
    if (global_price < 100 || global_price > 999999) {
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
```

---

## 🟡 BUG #7: Зависание в STATE_ERROR (ПРИОРИТЕТ 7)

### Решение:

```c
// В dispenser.c

case STATE_ERROR:
    UsbLog_Printf("[ERROR] Communication error, resetting\r\n");
    g_dispenser.is_connected = 0;
    t_command_sent = 0;  // ✅ Сброс флага T при ошибке
    
    // Уменьшенный таймаут
    if (IsStateTimeout(500)) {  // ✅ 500 мс вместо 2000 мс
        UsbLog_Printf("[ERROR] Recovery timeout, returning to IDLE\r\n");
        ChangeState(STATE_IDLE);
    }
    break;
```

---

## 🟢 BUG #10: Сброс t_command_sent при ошибке (ПРИОРИТЕТ 10)

Уже исправлено в BUG #7 выше.

---

## ⚠️ ДОПОЛНИТЕЛЬНЫЕ УЛУЧШЕНИЯ

### Добавить проверку результата UART передачи:

```c
static void SendFrame(char cmd, const char *data) {
    uint16_t len = Gas_BuildFrame(tx_buf, 0x00, 0x01, cmd, data);
    
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart2, tx_buf, len, 100);
    if (status != HAL_OK) {
        UsbLog_Printf("TX ERROR: cmd=%c, status=%d\r\n", cmd, status);
        // Можно установить флаг ошибки или перейти в STATE_ERROR
    }

    // Логируем команду
    if (data && data[0] != '\0') {
        UsbLog_Printf("TX: %c%s\r\n", cmd, data);
    } else {
        UsbLog_Printf("TX: %c\r\n", cmd);
    }
}
```

### Добавить Watchdog Timer:

```c
// В main.c, в USER CODE BEGIN 2:

/* Включить IWDG с таймаутом 1 секунда */
// В CubeMX: Watchdog -> IWDG -> Activated
// Prescaler = 32, Reload Value = 1000

// В main loop:
while (1)
{
    /* Сброс watchdog */
    HAL_IWDG_Refresh(&hiwdg);
    
    Dispenser_Update();
    SSD1309_Task(&oled);
    UsbLog_Task();
    UI_ProcessInput();
    
    if (SSD1309_IsReady(&oled)) {
        UI_Draw();
    }
}
```

---

## 📋 ЧЕКЛИСТ ПРИМЕНЕНИЯ ИСПРАВЛЕНИЙ

- [ ] BUG #1: Добавить критическую секцию для UART RX
- [ ] BUG #2: Исправить проверки границ input_buf
- [ ] BUG #3: Переместить сброс transaction_closed
- [ ] BUG #5: Убрать избыточную проверку в keyboard.c
- [ ] BUG #6: Добавить строгую валидацию EEPROM
- [ ] BUG #7: Уменьшить таймаут STATE_ERROR и сбросить t_command_sent
- [ ] Дополнительно: Проверка результата UART TX
- [ ] Дополнительно: Включить IWDG Watchdog

---

## 🧪 ТЕСТИРОВАНИЕ ПОСЛЕ ИСПРАВЛЕНИЙ

### Тест 1: UART Race Condition
1. Непрерывно отправлять данные от диспенсера (каждые 100 мс)
2. Проверить, что нет потерянных или повреждённых пакетов
3. Запустить на 1+ час

### Тест 2: Input Buffer
1. Попытаться ввести 20 цифр подряд
2. Проверить, что система не падает
3. Проверить логи на "Input buffer full!"

### Тест 3: Множественная N
1. Провести транзакцию до конца
2. Нажать F (Repeat)
3. НЕ начинать новую транзакцию
4. Проверить логи - команда N не должна отправляться повторно

### Тест 4: Watchdog
1. Искусственно создать бесконечный цикл в коде
2. Проверить, что система перезагружается через ~1 секунду

---

## ✅ ОЖИДАЕМЫЕ РЕЗУЛЬТАТЫ

После применения всех исправлений:
- ✅ Нет race conditions в UART
- ✅ Нет buffer overflows
- ✅ Нет зависаний системы
- ✅ Корректная работа протокола
- ✅ Watchdog защищает от deadlock
- ✅ Все edge cases обработаны
