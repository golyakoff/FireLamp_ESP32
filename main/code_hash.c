#include "code_hash.h"
#include <string.h>
#include <esp_crc.h>

// Codes are stored as their CRC16. A code whose CRC16 is already taken by another code gets
// a fixed replacement here. Append only: changing an entry loses the values stored for the code.
static const struct
{
    const char *code;
    uint16_t hash;
} overrides[] = {
    { "wheel", 0x289b }, // CRC16 0x1813 is taken by `relax`
};

uint16_t code_hash(const char *code)
{
    for (size_t i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++)
        if (!strcmp(code, overrides[i].code))
            return overrides[i].hash;
    return esp_crc16_le(0, (const uint8_t *)code, strlen(code));
}
