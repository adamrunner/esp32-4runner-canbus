/*
 * TPMS ECU diagnostic probe (Toyota, extended addressing)
 *
 * Request/response framing for the TPMS ECU at 0x750 / 0x758 with ISO-TP
 * extended address 0x2A, and decoders for the data identifiers listed in
 * OBDb's 4Runner signal set:
 *
 *   21 30    pressures FL, FR, RL, RR, spare  bar = raw / 58 - 0.5 (raw 0 = no data)
 *   21 15    pressures "set 2", tires 1-5     bar = raw * 69 / 4000 - 0.5
 *   22 1005  pressures FR, RR, FL, RL, spare  PSI = raw / 4.125 - 5.697, every other byte
 *   21 16    temperatures FL, FR, RL, RR, spare  C = raw - 40
 *
 * OBDb lists 21 30 for 2018, 21 15 for all years, 21 16 from 2020 and 22 1005
 * from 2026, but on this 2018 truck nothing answers at 0x750 ext 2A over the
 * DLC3 CAN (not even tester present), so the firmware does not poll these;
 * see docs/TPMS_DECODING_NOTES.md. Kept for other trucks and years.
 *
 * Responses longer than six bytes arrive as ISO-TP multi-frame messages, so
 * the caller must send a flow-control frame when the reassembler asks for one.
 * OBDb does not name the positions for 21 15; they are assumed to follow 21 30.
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

#define TPMS_DIAG_REQUEST_ID        0x750
#define TPMS_DIAG_RESPONSE_ID       0x758
#define TPMS_DIAG_EXT_ADDR          0x2A
#define TPMS_DIAG_SID               0x21
#define TPMS_DIAG_SID_UDS_READ      0x22
#define TPMS_DIAG_SID_TESTER        0x3E
#define TPMS_DIAG_PID_PRESSURE      0x30
#define TPMS_DIAG_PID_PRESSURE_SET2 0x15
#define TPMS_DIAG_PID_TEMP          0x16
#define TPMS_DIAG_DID_PRESSURE      0x1005
#define TPMS_DIAG_TIRE_COUNT        5   // FL, FR, RL, RR, spare
#define TPMS_DIAG_MAX_PAYLOAD       128  // room for KWP 1A identification strings

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
// the payload is not a positive response for that PID; the pressure decoder
// accepts 21 30, 21 15 and 22 1005 replies. Negative responses (7F <SID> NRC)
// set *nrc and return false; *nrc is 0 otherwise.
bool tpms_diag_decode_pressures(const uint8_t *payload, size_t len, tpms_diag_values_t *out,
                                uint8_t *nrc);
bool tpms_diag_decode_temps(const uint8_t *payload, size_t len, tpms_diag_values_t *out,
                            uint8_t *nrc);

#ifdef __cplusplus
}
#endif
