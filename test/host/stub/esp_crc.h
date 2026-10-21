#pragma once
#include <stdint.h>
static inline uint16_t esp_crc16_le(uint16_t crc, const uint8_t *buf, uint32_t len)
{
    crc = ~crc;
    while (len--) { crc ^= *buf++; for (int i = 0; i < 8; i++) crc = crc & 1 ? (crc >> 1) ^ 0x8408 : crc >> 1; }
    return ~crc;
}
