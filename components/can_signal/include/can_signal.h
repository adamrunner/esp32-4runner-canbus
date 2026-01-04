/*
 * CAN Signal Extraction Library
 *
 * Pure functions for extracting and decoding signals from CAN bus data.
 * No hardware dependencies - can be compiled for ESP32 or host testing.
 */

#ifndef CAN_SIGNAL_H
#define CAN_SIGNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract a big-endian signal where start_bit is the LSB position.
 *
 * This function handles the DBC "big-endian" (Motorola) byte order where
 * the start_bit indicates the position of the LSB of the signal. Bits are
 * collected from start_bit upward (in big-endian bit numbering) and
 * assembled so the first bit becomes the LSB of the result.
 *
 * Bit numbering within a byte: 7 6 5 4 3 2 1 0 (MSB to LSB)
 * Byte order: Big-endian (byte 0 is most significant)
 *
 * @param data      Pointer to CAN frame data (8 bytes)
 * @param start_bit Starting bit position (LSB of signal, 0-63)
 * @param length    Number of bits to extract (1-32)
 * @return          Extracted unsigned value
 */
uint32_t can_signal_extract_be_lsb(const uint8_t *data, uint8_t start_bit, uint8_t length);

/**
 * Sign-extend a value from bit_length bits to 32 bits.
 *
 * If the sign bit (MSB of the bit_length field) is set, the upper bits
 * of the result are filled with 1s to preserve the two's complement
 * signed value.
 *
 * @param value      Unsigned value to sign-extend
 * @param bit_length Original bit width of the value (1-31)
 * @return           Sign-extended 32-bit signed value
 */
int32_t can_signal_sign_extend(uint32_t value, uint8_t bit_length);

/**
 * Extract and sign-extend a big-endian signal in one operation.
 *
 * Combines can_signal_extract_be_lsb and can_signal_sign_extend for
 * signed signal values.
 *
 * @param data      Pointer to CAN frame data (8 bytes)
 * @param start_bit Starting bit position (LSB of signal, 0-63)
 * @param length    Number of bits to extract (1-32)
 * @return          Extracted signed value
 */
int32_t can_signal_extract_be_lsb_signed(const uint8_t *data, uint8_t start_bit, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif /* CAN_SIGNAL_H */
