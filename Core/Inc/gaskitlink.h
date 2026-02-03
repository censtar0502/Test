#ifndef GASKITLINK_H
#define GASKITLINK_H

#include <stdint.h>
#include <string.h>

#define GAS_STX 0x02

typedef struct {
    uint8_t addr_high;
    uint8_t addr_low;
    uint8_t cmd;
    char data[23];
    uint8_t data_len;
} GasFrame_t;

uint8_t Gas_CalculateCRC(const uint8_t *data, uint16_t len);
uint16_t Gas_BuildFrame(uint8_t *buffer, uint8_t addr_high, uint8_t addr_low, char cmd, const char *data);
int Gas_ParseFrame(const uint8_t *buffer, uint16_t len, GasFrame_t *frame);

#endif // GASKITLINK_H
