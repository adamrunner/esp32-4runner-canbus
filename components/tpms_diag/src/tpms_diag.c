#include "tpms_diag.h"

#include <string.h>

#define PCI_SINGLE       0x0
#define PCI_FIRST        0x1
#define PCI_CONSECUTIVE  0x2
#define PCI_FLOW_CONTROL 0x3

#define SID_NEGATIVE     0x7F
#define POSITIVE_OFFSET  0x40

#define PSI_PER_BAR      14.5038f
#define PRESSURE_DIV     58.0f
#define PRESSURE_ADD_BAR (-0.5f)
#define TEMP_ADD_C       (-40.0f)

void tpms_diag_build_request(uint8_t pid, uint8_t out[8])
{
    memset(out, 0, 8);
    out[0] = TPMS_DIAG_EXT_ADDR;
    out[1] = 0x02;  // single frame, 2 bytes
    out[2] = TPMS_DIAG_SID;
    out[3] = pid;
}

void tpms_diag_build_flow_control(uint8_t out[8])
{
    memset(out, 0, 8);
    out[0] = TPMS_DIAG_EXT_ADDR;
    out[1] = PCI_FLOW_CONTROL << 4;  // 0x30: continue to send
    out[2] = 0x00;                   // block size: send everything
    out[3] = 0x00;                   // separation time: none
}

void tpms_diag_rx_reset(tpms_diag_rx_t *rx)
{
    memset(rx, 0, sizeof(*rx));
}

tpms_diag_rx_result_t tpms_diag_rx_feed(tpms_diag_rx_t *rx, const uint8_t *data, uint8_t dlc)
{
    if (!rx || !data || dlc < 2) {
        return TPMS_DIAG_RX_NONE;
    }

    // Extended addressing: byte 0 is the address, ISO-TP starts at byte 1.
    uint8_t pci = data[1];
    const uint8_t *body = data + 2;
    size_t body_len = dlc - 2;

    switch (pci >> 4) {
    case PCI_SINGLE: {
        size_t len = pci & 0x0F;
        if (len == 0 || len > body_len) {
            tpms_diag_rx_reset(rx);
            return TPMS_DIAG_RX_ERROR;
        }
        tpms_diag_rx_reset(rx);
        memcpy(rx->payload, body, len);
        rx->expected_len = rx->received_len = len;
        return TPMS_DIAG_RX_COMPLETE;
    }
    case PCI_FIRST: {
        if (dlc < 3) {
            tpms_diag_rx_reset(rx);
            return TPMS_DIAG_RX_ERROR;
        }
        size_t len = ((size_t)(pci & 0x0F) << 8) | data[2];
        body = data + 3;
        body_len = dlc - 3;
        if (len <= body_len || len > TPMS_DIAG_MAX_PAYLOAD) {
            tpms_diag_rx_reset(rx);
            return TPMS_DIAG_RX_ERROR;
        }
        tpms_diag_rx_reset(rx);
        memcpy(rx->payload, body, body_len);
        rx->expected_len = len;
        rx->received_len = body_len;
        rx->next_sn = 1;
        rx->in_progress = true;
        return TPMS_DIAG_RX_SEND_FC;
    }
    case PCI_CONSECUTIVE: {
        if (!rx->in_progress) {
            return TPMS_DIAG_RX_NONE;  // stray, e.g. tail of a message we joined late
        }
        if ((pci & 0x0F) != rx->next_sn) {
            tpms_diag_rx_reset(rx);
            return TPMS_DIAG_RX_ERROR;
        }
        size_t remaining = rx->expected_len - rx->received_len;
        size_t n = body_len < remaining ? body_len : remaining;
        memcpy(rx->payload + rx->received_len, body, n);
        rx->received_len += n;
        rx->next_sn = (rx->next_sn + 1) & 0x0F;
        if (rx->received_len < rx->expected_len) {
            return TPMS_DIAG_RX_NONE;
        }
        rx->in_progress = false;
        return TPMS_DIAG_RX_COMPLETE;
    }
    default:
        return TPMS_DIAG_RX_NONE;  // flow control from the other side; not ours to handle
    }
}

static bool decode_common(const uint8_t *payload, size_t len, uint8_t pid,
                          tpms_diag_values_t *out, uint8_t *nrc)
{
    if (nrc) {
        *nrc = 0;
    }
    if (!payload || !out || len < 2) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (payload[0] == SID_NEGATIVE) {
        if (nrc && len >= 3 && payload[1] == TPMS_DIAG_SID) {
            *nrc = payload[2];
        }
        return false;
    }
    return payload[0] == TPMS_DIAG_SID + POSITIVE_OFFSET && payload[1] == pid;
}

bool tpms_diag_decode_pressures(const uint8_t *payload, size_t len, tpms_diag_values_t *out,
                                uint8_t *nrc)
{
    if (!decode_common(payload, len, TPMS_DIAG_PID_PRESSURE, out, nrc)) {
        return false;
    }
    for (size_t i = 0; i < TPMS_DIAG_TIRE_COUNT && 2 + i < len; i++) {
        uint8_t raw = payload[2 + i];
        if (raw == 0) {
            continue;  // OBDb nullmin: no reading for this wheel
        }
        float bar = raw / PRESSURE_DIV + PRESSURE_ADD_BAR;
        out->value[i] = bar * PSI_PER_BAR;
        out->valid[i] = true;
    }
    return true;
}

bool tpms_diag_decode_temps(const uint8_t *payload, size_t len, tpms_diag_values_t *out,
                            uint8_t *nrc)
{
    if (!decode_common(payload, len, TPMS_DIAG_PID_TEMP, out, nrc)) {
        return false;
    }
    for (size_t i = 0; i < TPMS_DIAG_TIRE_COUNT && 2 + i < len; i++) {
        uint8_t raw = payload[2 + i];
        if (raw == 0) {
            continue;  // OBDb nullmin (-40 C)
        }
        out->value[i] = raw + TEMP_ADD_C;
        out->valid[i] = true;
    }
    return true;
}
