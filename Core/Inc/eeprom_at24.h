#ifndef EEPROM_AT24_H
#define EEPROM_AT24_H

#include "main.h"
#include "i2c.h"

#define AT24C256_ADDR 0xA0

HAL_StatusTypeDef EEPROM_Write(uint16_t mem_addr, uint8_t *data, uint16_t size);
HAL_StatusTypeDef EEPROM_Read(uint16_t mem_addr, uint8_t *data, uint16_t size);

// Specific helpers for this project
HAL_StatusTypeDef EEPROM_SavePrice(uint32_t price);
uint32_t EEPROM_LoadPrice(void);

#endif // EEPROM_AT24_H
