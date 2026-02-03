#include "gaskitlink.h"

uint8_t Gas_CalculateCRC(const uint8_t *data, uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

uint16_t Gas_BuildFrame(uint8_t *buffer, uint8_t addr_high, uint8_t addr_low, char cmd, const char *data) {
    uint16_t pos = 0;
    buffer[pos++] = GAS_STX;
    buffer[pos++] = addr_high;
    buffer[pos++] = addr_low;
    buffer[pos++] = (uint8_t)cmd;
    
    if (data) {
        uint16_t data_len = (uint16_t)strlen(data);
        if (data_len > 22) data_len = 22;
        memcpy(&buffer[pos], data, data_len);
        pos += data_len;
    }
    
    buffer[pos] = Gas_CalculateCRC(&buffer[1], pos - 1);
    pos++;
    
    return pos;
}

int Gas_ParseFrame(const uint8_t *buffer, uint16_t len, GasFrame_t *frame) {
    if (len < 5) return -1; // Too short
    if (buffer[0] != GAS_STX) return -2; // No STX
    
    uint8_t crc_calc = Gas_CalculateCRC(&buffer[1], len - 2);
    if (crc_calc != buffer[len - 1]) return -3; // CRC Error
    
    frame->addr_high = buffer[1];
    frame->addr_low = buffer[2];
    frame->cmd = buffer[3];
    
    uint8_t data_len = len - 5;
    if (data_len > 22) data_len = 22;
    frame->data_len = data_len;
    memcpy(frame->data, &buffer[4], data_len);
    frame->data[data_len] = '\0';
    
    return 0;
}
