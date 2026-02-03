#include "eeprom_at24.h"

#define EEPROM_PRICE_ADDR 0x0000

HAL_StatusTypeDef EEPROM_Write(uint16_t mem_addr, uint8_t *data, uint16_t size) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, AT24C256_ADDR, mem_addr, I2C_MEMADD_SIZE_16BIT, data, size, 100);
    if (status == HAL_OK) {
        HAL_Delay(5); // Typical write cycle time for AT24C
    }
    return status;
}

HAL_StatusTypeDef EEPROM_Read(uint16_t mem_addr, uint8_t *data, uint16_t size) {
    return HAL_I2C_Mem_Read(&hi2c1, AT24C256_ADDR, mem_addr, I2C_MEMADD_SIZE_16BIT, data, size, 100);
}

HAL_StatusTypeDef EEPROM_SavePrice(uint32_t price) {
    uint8_t buf[4];
    buf[0] = (price >> 24) & 0xFF;
    buf[1] = (price >> 16) & 0xFF;
    buf[2] = (price >> 8) & 0xFF;
    buf[3] = price & 0xFF;
    return EEPROM_Write(EEPROM_PRICE_ADDR, buf, 4);
}

uint32_t EEPROM_LoadPrice(void) {
    uint8_t buf[4];
    if (EEPROM_Read(EEPROM_PRICE_ADDR, buf, 4) != HAL_OK) return 0;
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}
