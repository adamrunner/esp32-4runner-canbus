/*
 * Unit tests for the TPMS ECU diagnostic probe (ISO-TP extended addressing)
 */

#include "unity/unity.h"
#include "tpms_diag.h"
#include <string.h>

static tpms_diag_rx_t rx;

void setUp(void) {
    tpms_diag_rx_reset(&rx);
}

void tearDown(void) {
}

void test_build_request(void) {
    uint8_t frame[8];
    tpms_diag_build_request(TPMS_DIAG_PID_PRESSURE, frame);
    uint8_t expected[8] = {0x2A, 0x02, 0x21, 0x30, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 8);
}

void test_build_flow_control(void) {
    uint8_t frame[8];
    tpms_diag_build_flow_control(frame);
    uint8_t expected[8] = {0x2A, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 8);
}

/*
 * 61 30 + five pressures = 7 bytes: too long for one extended-address frame
 * (6 bytes), so the ECU sends a first frame and waits for flow control.
 */
void test_multi_frame_pressures(void) {
    uint8_t ff[8] = {0x2A, 0x10, 0x07, 0x61, 0x30, 0xB5, 0xB5, 0xB4};
    uint8_t cf[8] = {0x2A, 0x21, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_SEND_FC, tpms_diag_rx_feed(&rx, ff, 8));
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_COMPLETE, tpms_diag_rx_feed(&rx, cf, 8));
    TEST_ASSERT_EQUAL(7, rx.received_len);

    tpms_diag_values_t v;
    uint8_t nrc = 0xFF;
    TEST_ASSERT_TRUE(tpms_diag_decode_pressures(rx.payload, rx.received_len, &v, &nrc));
    TEST_ASSERT_EQUAL_UINT8(0, nrc);
    // raw 181: (181 / 58 - 0.5) bar = 2.6207 bar = 38.0 PSI
    TEST_ASSERT_TRUE(v.valid[TPMS_DIAG_TIRE_FL]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 38.01f, v.value[TPMS_DIAG_TIRE_FL]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 37.76f, v.value[TPMS_DIAG_TIRE_RL]);  // raw 180
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 38.26f, v.value[TPMS_DIAG_TIRE_RR]);  // raw 182
    TEST_ASSERT_FALSE(v.valid[TPMS_DIAG_TIRE_SPARE]);                     // raw 0 = no data
}

void test_single_frame_negative_response(void) {
    // 7F 21 12: subFunctionNotSupported / invalid format
    uint8_t sf[8] = {0x2A, 0x03, 0x7F, 0x21, 0x12, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_COMPLETE, tpms_diag_rx_feed(&rx, sf, 8));

    tpms_diag_values_t v;
    uint8_t nrc = 0;
    TEST_ASSERT_FALSE(tpms_diag_decode_pressures(rx.payload, rx.received_len, &v, &nrc));
    TEST_ASSERT_EQUAL_UINT8(0x12, nrc);
}

void test_single_frame_temps(void) {
    // A shorter reply fits in one frame: 61 16 + four temps
    uint8_t sf[8] = {0x2A, 0x06, 0x61, 0x16, 0x3C, 0x3D, 0x00, 0x3E};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_COMPLETE, tpms_diag_rx_feed(&rx, sf, 8));

    tpms_diag_values_t v;
    TEST_ASSERT_TRUE(tpms_diag_decode_temps(rx.payload, rx.received_len, &v, NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, v.value[TPMS_DIAG_TIRE_FL]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, v.value[TPMS_DIAG_TIRE_FR]);
    TEST_ASSERT_FALSE(v.valid[TPMS_DIAG_TIRE_RL]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, v.value[TPMS_DIAG_TIRE_RR]);
    TEST_ASSERT_FALSE(v.valid[TPMS_DIAG_TIRE_SPARE]);  // not in this payload
}

void test_wrong_pid_is_rejected(void) {
    uint8_t payload[] = {0x61, 0x16, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C};
    tpms_diag_values_t v;
    TEST_ASSERT_FALSE(tpms_diag_decode_pressures(payload, sizeof(payload), &v, NULL));
}

void test_out_of_sequence_consecutive_frame(void) {
    uint8_t ff[8] = {0x2A, 0x10, 0x07, 0x61, 0x30, 0xB5, 0xB5, 0xB4};
    uint8_t cf_bad[8] = {0x2A, 0x22, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_SEND_FC, tpms_diag_rx_feed(&rx, ff, 8));
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_ERROR, tpms_diag_rx_feed(&rx, cf_bad, 8));
    TEST_ASSERT_FALSE(rx.in_progress);
}

void test_stray_consecutive_frame_is_ignored(void) {
    uint8_t cf[8] = {0x2A, 0x21, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_NONE, tpms_diag_rx_feed(&rx, cf, 8));
}

void test_oversized_first_frame_is_rejected(void) {
    uint8_t ff[8] = {0x2A, 0x10, 0xFF, 0x61, 0x30, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_ERROR, tpms_diag_rx_feed(&rx, ff, 8));
}

void test_new_first_frame_restarts_reassembly(void) {
    uint8_t ff[8] = {0x2A, 0x10, 0x07, 0x61, 0x30, 0x01, 0x02, 0x03};
    uint8_t ff2[8] = {0x2A, 0x10, 0x07, 0x61, 0x30, 0xB5, 0xB5, 0xB4};
    uint8_t cf[8] = {0x2A, 0x21, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_SEND_FC, tpms_diag_rx_feed(&rx, ff, 8));
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_SEND_FC, tpms_diag_rx_feed(&rx, ff2, 8));
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_COMPLETE, tpms_diag_rx_feed(&rx, cf, 8));
    TEST_ASSERT_EQUAL_UINT8(0xB5, rx.payload[2]);
}

void test_short_frames_are_ignored(void) {
    uint8_t f[1] = {0x2A};
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_NONE, tpms_diag_rx_feed(&rx, f, 1));
    TEST_ASSERT_EQUAL(TPMS_DIAG_RX_NONE, tpms_diag_rx_feed(&rx, NULL, 8));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_build_request);
    RUN_TEST(test_build_flow_control);
    RUN_TEST(test_multi_frame_pressures);
    RUN_TEST(test_single_frame_negative_response);
    RUN_TEST(test_single_frame_temps);
    RUN_TEST(test_wrong_pid_is_rejected);
    RUN_TEST(test_out_of_sequence_consecutive_frame);
    RUN_TEST(test_stray_consecutive_frame_is_ignored);
    RUN_TEST(test_oversized_first_frame_is_rejected);
    RUN_TEST(test_new_first_frame_restarts_reassembly);
    RUN_TEST(test_short_frames_are_ignored);

    return UNITY_END();
}
