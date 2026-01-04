/*
 * Unit tests for CAN signal extraction functions
 *
 * These tests verify correct bit extraction from CAN frames using
 * DBC big-endian (Motorola) byte order with LSB start bit notation.
 *
 * Bit ordering in DBC big-endian LSB-start format:
 * - start_bit indicates the position of the signal's LSB
 * - Bits are traversed: within a byte from high to low (7->6->...->0)
 * - At bit 0, wrap to the next byte's bit 7
 * - Extracted bits are placed LSB-first in the result
 */

#include "unity/unity.h"
#include "can_signal.h"
#include <string.h>

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

/*
 * Test: Extract single bit values
 */
void test_extract_single_bit(void) {
    // Bit 0 of byte 0 = 1
    uint8_t data1[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT32(1, can_signal_extract_be_lsb(data1, 0, 1));

    // Bit 7 of byte 0 = 1
    uint8_t data2[8] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT32(1, can_signal_extract_be_lsb(data2, 7, 1));

    // Bit 3 of byte 0 = 1
    uint8_t data3[8] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT32(1, can_signal_extract_be_lsb(data3, 3, 1));
}

/*
 * Test: Bit order is correct (regression test for bit-reversal bug)
 *
 * This test specifically catches the bug where:
 *   value = (value << 1) | bit;  // WRONG: first bit becomes MSB
 * vs the correct:
 *   value |= bit << i;           // CORRECT: first bit becomes LSB
 */
void test_bit_order_not_reversed(void) {
    // Set bit 0 of byte 0 = 1, bit 7 of byte 1 = 0
    // With start_bit=0, length=2:
    // - i=0: extract bit 0 of byte 0 (=1) -> result bit 0
    // - i=1: wrap to byte 1 bit 7 (=0) -> result bit 1
    // Result should be 1 (binary: 01), NOT 2 (binary: 10)
    uint8_t data[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint32_t result = can_signal_extract_be_lsb(data, 0, 2);
    TEST_ASSERT_EQUAL_UINT32(1, result);
    TEST_ASSERT_NOT_EQUAL(2, result);  // Would be 2 if bit order was reversed
}

/*
 * Test: 2-bit extraction across byte boundary
 */
void test_2bit_cross_boundary(void) {
    // Set bit 0 of byte 0 = 1, bit 7 of byte 1 = 1
    // Result should be 3 (binary: 11)
    uint8_t data[8] = {0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t result = can_signal_extract_be_lsb(data, 0, 2);
    TEST_ASSERT_EQUAL_UINT32(3, result);
}

/*
 * Test: Zero value extraction
 */
void test_extract_zero(void) {
    uint8_t data[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_EQUAL_UINT32(0, can_signal_extract_be_lsb(data, 0, 8));
    TEST_ASSERT_EQUAL_UINT32(0, can_signal_extract_be_lsb(data, 1, 10));
    TEST_ASSERT_EQUAL_UINT32(0, can_signal_extract_be_lsb(data, 3, 12));
}

/*
 * Test: Maximum value for various bit lengths
 */
void test_extract_max_values(void) {
    uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // 1-bit max = 1
    TEST_ASSERT_EQUAL_UINT32(0x01, can_signal_extract_be_lsb(data, 0, 1));

    // 10-bit max = 1023
    TEST_ASSERT_EQUAL_UINT32(0x3FF, can_signal_extract_be_lsb(data, 1, 10));

    // 12-bit max = 4095
    TEST_ASSERT_EQUAL_UINT32(0xFFF, can_signal_extract_be_lsb(data, 3, 12));
}

/*
 * Test: 10-bit signal extraction (like kinematics yaw rate)
 *
 * Construct a frame to produce a known 10-bit value.
 * Value 512 = 0x200 = binary 1000000000
 *
 * For start_bit=1, length=10, bits are extracted:
 * - bit 0 of signal from byte0_bit1
 * - bit 1 of signal from byte0_bit0
 * - bit 2 of signal from byte1_bit7
 * - ...
 * - bit 9 of signal from byte1_bit0
 *
 * To get value 512 (bit 9 set, others 0):
 * - byte1_bit0 = 1
 * So byte1 = 0x01
 */
void test_10bit_signal_value_512(void) {
    uint8_t data[8] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t result = can_signal_extract_be_lsb(data, 1, 10);
    TEST_ASSERT_EQUAL_UINT32(512, result);
}

/*
 * Test: 10-bit signal with value 1 (only LSB set)
 *
 * For start_bit=1, length=10:
 * - bit 0 of signal from byte0_bit1
 * So byte0 = 0x02 (bit 1 set)
 */
void test_10bit_signal_value_1(void) {
    uint8_t data[8] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t result = can_signal_extract_be_lsb(data, 1, 10);
    TEST_ASSERT_EQUAL_UINT32(1, result);
}

/*
 * Test: 10-bit signal with value 513 (bits 0 and 9 set)
 */
void test_10bit_signal_value_513(void) {
    // byte0_bit1 = 1 (signal bit 0)
    // byte1_bit0 = 1 (signal bit 9)
    uint8_t data[8] = {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t result = can_signal_extract_be_lsb(data, 1, 10);
    TEST_ASSERT_EQUAL_UINT32(513, result);
}

/*
 * Test: 12-bit steering angle signal (start_bit=3)
 *
 * For value 30 = 0x1E = binary 000000011110:
 * - bit 0 = 0 -> byte0_bit3
 * - bit 1 = 1 -> byte0_bit2
 * - bit 2 = 1 -> byte0_bit1
 * - bit 3 = 1 -> byte0_bit0
 * - bit 4 = 1 -> byte1_bit7
 * - bits 5-11 = 0
 *
 * byte0 = 0x07 (bits 0,1,2 set)
 * byte1 = 0x80 (bit 7 set)
 */
void test_12bit_steering_angle_value_30(void) {
    uint8_t data[8] = {0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t result = can_signal_extract_be_lsb(data, 3, 12);
    TEST_ASSERT_EQUAL_UINT32(30, result);
}

/*
 * Test: 12-bit signal with value 4095 (all bits set)
 *
 * For start_bit=3, length=12:
 * All 12 bits from byte0_bit3 through byte1_bit0 should be 1
 * - byte0 bits 3,2,1,0 = 0x0F
 * - byte1 all bits = 0xFF
 */
void test_12bit_signal_max(void) {
    uint8_t data[8] = {0x0F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t result = can_signal_extract_be_lsb(data, 3, 12);
    TEST_ASSERT_EQUAL_UINT32(4095, result);
}

/*
 * Test: Sign extension of positive values
 */
void test_sign_extend_positive(void) {
    // 10-bit value 511 (max positive for signed 10-bit: 0x1FF)
    // Sign bit (bit 9) is 0, so no extension needed
    TEST_ASSERT_EQUAL_INT32(511, can_signal_sign_extend(511, 10));

    // 12-bit value 2047 (max positive for signed 12-bit)
    TEST_ASSERT_EQUAL_INT32(2047, can_signal_sign_extend(2047, 12));
}

/*
 * Test: Sign extension of negative values
 */
void test_sign_extend_negative(void) {
    // 10-bit value 512 has sign bit set -> should become -512
    // 512 = 0x200, sign-extended to 32-bit = 0xFFFFFE00 = -512
    TEST_ASSERT_EQUAL_INT32(-512, can_signal_sign_extend(512, 10));

    // 10-bit value 1023 (all 1s) -> should become -1
    TEST_ASSERT_EQUAL_INT32(-1, can_signal_sign_extend(1023, 10));

    // 12-bit value 2048 has sign bit set -> should become -2048
    TEST_ASSERT_EQUAL_INT32(-2048, can_signal_sign_extend(2048, 12));
}

/*
 * Test: Sign extension edge cases
 */
void test_sign_extend_edge_cases(void) {
    // 0-bit length returns value unchanged
    TEST_ASSERT_EQUAL_INT32(0, can_signal_sign_extend(0, 0));
    TEST_ASSERT_EQUAL_INT32(123, can_signal_sign_extend(123, 0));

    // 32-bit length returns value unchanged (no extension needed)
    TEST_ASSERT_EQUAL_INT32(-1, can_signal_sign_extend(0xFFFFFFFF, 32));
    TEST_ASSERT_EQUAL_INT32(0x7FFFFFFF, can_signal_sign_extend(0x7FFFFFFF, 32));

    // 1-bit value: 0 stays 0, 1 becomes -1
    TEST_ASSERT_EQUAL_INT32(0, can_signal_sign_extend(0, 1));
    TEST_ASSERT_EQUAL_INT32(-1, can_signal_sign_extend(1, 1));
}

/*
 * Test: Combined extract and sign-extend for negative value
 *
 * 12-bit signal with value -1 (all 1s = 0xFFF)
 * For start_bit=3:
 * - byte0 bits 3,2,1,0 = 0x0F
 * - byte1 = 0xFF
 */
void test_extract_signed_negative(void) {
    uint8_t data[8] = {0x0F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    int32_t result = can_signal_extract_be_lsb_signed(data, 3, 12);
    TEST_ASSERT_EQUAL_INT32(-1, result);
}

/*
 * Test: Combined extract and sign-extend for positive value
 */
void test_extract_signed_positive(void) {
    // 12-bit value 30 (positive, no sign extension changes value)
    uint8_t data[8] = {0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    int32_t result = can_signal_extract_be_lsb_signed(data, 3, 12);
    TEST_ASSERT_EQUAL_INT32(30, result);
}

/*
 * Test: NULL data pointer handling
 */
void test_null_data_handling(void) {
    TEST_ASSERT_EQUAL_UINT32(0, can_signal_extract_be_lsb(NULL, 0, 8));
}

/*
 * Test: Invalid length handling
 */
void test_invalid_length_handling(void) {
    uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Length 0 should return 0
    TEST_ASSERT_EQUAL_UINT32(0, can_signal_extract_be_lsb(data, 0, 0));

    // Length > 32 should return 0
    TEST_ASSERT_EQUAL_UINT32(0, can_signal_extract_be_lsb(data, 0, 33));
}

/*
 * Test: Simulated real kinematics frame (CAN ID 0x024)
 *
 * Create a frame with known signal values and verify extraction
 * of all three kinematics signals.
 */
void test_kinematics_frame_simulation(void) {
    // Construct a frame with:
    // - Yaw rate at start_bit=1, length=10: value = 512 (zero point)
    // - Torque at start_bit=17, length=10: value = 256
    // - Accel at start_bit=33, length=10: value = 768

    uint8_t data[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Yaw rate = 512: byte1_bit0 = 1
    data[1] = 0x01;

    // Torque = 256 at start_bit=17:
    // - bit 0 of signal from byte2_bit1
    // - bit 8 (value 256) from byte3_bit1
    // So byte3 = 0x02
    data[3] = 0x02;

    // Accel = 768 at start_bit=33:
    // - 768 = 512 + 256 = bits 8 and 9 set
    // - bit 8 from byte5_bit0
    // - bit 9 from byte5_bit1 (wait, need to trace this more carefully)
    // Actually, start_bit=33 means byte4_bit1 is signal bit 0
    // bit 8 from byte5_bit1
    // bit 9 from byte5_bit0
    // 768 = 0x300, so we need bits 8 and 9: byte5 = 0x03
    data[5] = 0x03;

    // Verify extractions
    TEST_ASSERT_EQUAL_UINT32(512, can_signal_extract_be_lsb(data, 1, 10));
    TEST_ASSERT_EQUAL_UINT32(256, can_signal_extract_be_lsb(data, 17, 10));
    TEST_ASSERT_EQUAL_UINT32(768, can_signal_extract_be_lsb(data, 33, 10));
}

int main(void) {
    UNITY_BEGIN();

    // Single bit tests
    RUN_TEST(test_extract_single_bit);

    // Bit order tests (regression prevention)
    RUN_TEST(test_bit_order_not_reversed);
    RUN_TEST(test_2bit_cross_boundary);

    // Basic extraction tests
    RUN_TEST(test_extract_zero);
    RUN_TEST(test_extract_max_values);

    // 10-bit signal tests (kinematics)
    RUN_TEST(test_10bit_signal_value_512);
    RUN_TEST(test_10bit_signal_value_1);
    RUN_TEST(test_10bit_signal_value_513);

    // 12-bit signal tests (steering angle)
    RUN_TEST(test_12bit_steering_angle_value_30);
    RUN_TEST(test_12bit_signal_max);

    // Sign extension tests
    RUN_TEST(test_sign_extend_positive);
    RUN_TEST(test_sign_extend_negative);
    RUN_TEST(test_sign_extend_edge_cases);

    // Combined extract+sign-extend tests
    RUN_TEST(test_extract_signed_negative);
    RUN_TEST(test_extract_signed_positive);

    // Edge case tests
    RUN_TEST(test_null_data_handling);
    RUN_TEST(test_invalid_length_handling);

    // Real-world simulation
    RUN_TEST(test_kinematics_frame_simulation);

    return UNITY_END();
}
