/*
 * TPMS ECU diagnostic probe (Toyota, extended addressing)
 *
 * Request/response framing for the TPMS ECU at 0x750 / 0x758 with ISO-TP
 * extended address 0x2A, and decoders for the two data identifiers listed in
 * OBDb's 4Runner signal set:
 *
 *   21 30  pressures FL, FR, RL, RR, spare   bar = raw / 58 - 0.5 (raw 0 = no data)
 *   21 16  temperatures, same order          C   = raw - 40
 *
 * Neither has been confirmed on this truck yet; OBDb lists 21 30 for 2018 and
 * 21 16 only from 2020. Responses longer than six bytes arrive as ISO-TP
 * multi-frame messages, so the caller must send a flow-control frame when
 * the reassembler asks for one.
 *
 * Pure C with no ESP-IDF dependencies, so it can be host-tested.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TPMS_DIAG_REQUEST_ID   0x750
#define TPMS_DIAG_RESPONSE_ID  0x758
#define TPMS_DIAG_EXT_ADDR     0x2A
#define TPMS_DIAG_SID          0x21
#define TPMS_DIAG_PID_PRESSURE 0x30
#define TPMS_DIAG_PID_TEMP     0x16
#define TPMS_DIAG_TIRE_COUNT   5   // FL, FR, RL, RR, spare
#define TPMS_DIAG_MAX_PAYLOAD  64

typedef enum {
    TPMS_DIAG_TIRE_FL,
    TPMS_DIAG_TIRE_FR,
    TPMS_DIAG_TIRE_RL,
    TPMS_DIAG_TIRE_RR,
    TPMS_DIAG_TIRE_SPARE,
} tpms_diag_tire_t;

typedef enum {
    TPMS_DIAG_RX_NONE,      // frame consumed (or ignored), nothing to do
    TPMS_DIAG_RX_SEND_FC,   // first frame received: send the flow-control frame now
    TPMS_DIAG_RX_COMPLETE,  // payload is ready
    TPMS_DIAG_RX_ERROR,     // malformed or out-of-sequence frame; state was reset
} tpms_diag_rx_result_t;

typedef struct {
    uint8_t payload[TPMS_DIAG_MAX_PAYLOAD];
    size_t expected_len;
    size_t received_len;
    uint8_t next_sn;
    bool in_progress;
} tpms_diag_rx_t;

typedef struct {
    float value[TPMS_DIAG_TIRE_COUNT];  // PSI (gauge) or degrees C
    bool valid[TPMS_DIAG_TIRE_COUNT];
} tpms_diag_values_t;

// Build the 8-byte request frame for `pid` (TPMS_DIAG_PID_*).
void tpms_diag_build_request(uint8_t pid, uint8_t out[8]);

// Build the 8-byte flow-control frame (continue to send, no block limit, no gap).
void tpms_diag_build_flow_control(uint8_t out[8]);

void tpms_diag_rx_reset(tpms_diag_rx_t *rx);

// Feed one frame received on TPMS_DIAG_RESPONSE_ID. data[0] is the extended
// address byte, data[1] the ISO-TP PCI.
tpms_diag_rx_result_t tpms_diag_rx_feed(tpms_diag_rx_t *rx, const uint8_t *data, uint8_t dlc);

// Decode a complete payload (starting at the response SID). Returns false if
// the payload is not a positive response for that PID. Negative responses
// (7F 21 NRC) set *nrc and return false; *nrc is 0 otherwise.
bool tpms_diag_decode_pressures(const uint8_t *payload, size_t len, tpms_diag_values_t *out,
                                uint8_t *nrc);
bool tpms_diag_decode_temps(const uint8_t *payload, size_t len, tpms_diag_values_t *out,
                            uint8_t *nrc);

#ifdef __cplusplus
}
#endif
