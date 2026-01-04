/*
 * CAN Signal Extraction Library - Implementation
 */

#include "can_signal.h"
#include <stddef.h>

uint32_t can_signal_extract_be_lsb(const uint8_t *data, uint8_t start_bit, uint8_t length)
{
    if (data == NULL || length == 0 || length > 32) {
        return 0;
    }

    if (start_bit >= 64) {
        return 0;
    }

    uint32_t value = 0;
    int byte_index = start_bit / 8;
    int bit_index = start_bit % 8;  // 0 = LSB of byte

    for (uint8_t i = 0; i < length; i++) {
        // Bounds check for byte_index
        if (byte_index < 0 || byte_index >= 8) {
            break;
        }

        uint8_t bit = (data[byte_index] >> bit_index) & 0x01;
        value |= (uint32_t)bit << i;  // First bit -> bit 0 (LSB)

        // Move to next bit in big-endian order
        if (bit_index == 0) {
            byte_index++;
            bit_index = 7;
        } else {
            bit_index--;
        }
    }

    return value;
}

int32_t can_signal_sign_extend(uint32_t value, uint8_t bit_length)
{
    // No extension needed for 0 bits or full 32-bit values
    // Also avoids undefined behavior from shifting by >= 32
    if (bit_length == 0 || bit_length >= 32) {
        return (int32_t)value;
    }

    uint32_t sign_bit = 1u << (bit_length - 1);
    if (value & sign_bit) {
        // Sign bit is set, fill upper bits with 1s
        uint32_t mask = (1u << bit_length) - 1u;
        value |= ~mask;
    }

    return (int32_t)value;
}

int32_t can_signal_extract_be_lsb_signed(const uint8_t *data, uint8_t start_bit, uint8_t length)
{
    uint32_t raw = can_signal_extract_be_lsb(data, start_bit, length);
    return can_signal_sign_extend(raw, length);
}
