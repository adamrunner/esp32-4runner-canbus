/******************************************************************************
 * Toyota 4Runner (5th Gen 2010-2024) CAN Bus Decoder - VERIFIED FORMULAS
 * Based on actual capture data analysis
 * 
 * CAN Bus Speed: 500 kbps
 * All values are for passive listening (not OBD-II queries)
 * 
 * ✅ VERIFIED: Tire pressure formula (tested against 0x1A6F = 32.7 PSI)
 ******************************************************************************/

#ifndef TOYOTA_4RUNNER_CAN_DECODER_VERIFIED_H
#define TOYOTA_4RUNNER_CAN_DECODER_VERIFIED_H

#include <stdint.h>
#include <stdbool.h>

/******************************************************************************
 * TIRE PRESSURE MONITORING SYSTEM (TPMS) - ✅ VERIFIED
 ******************************************************************************/

// CAN ID: 0x0AA (Broadcast @ ~24Hz)
// DLC: 8 bytes
// Format: FL_H FL_L FR_H FR_L RL_H RL_L RR_H RR_L
typedef struct {
    float front_left_psi;
    float front_right_psi;
    float rear_left_psi;
    float rear_right_psi;
} tire_pressure_t;

static inline tire_pressure_t decode_tire_pressure(const uint8_t* data) {
    tire_pressure_t tp;
    
    // ✅ VERIFIED FORMULA: raw / 30 = kPa, then * 0.145038 = PSI
    // Alternatively: raw / 3000 = bar, then * 14.504 = PSI (equivalent)
    
    for (int i = 0; i < 4; i++) {
        uint16_t raw = (data[i*2] << 8) | data[i*2 + 1];
        
        // Method 1: Direct to PSI
        float psi = (raw / 30.0) * 0.145038;
        
        // Method 2: Via bar (produces same result)
        // float bar = raw / 3000.0;
        // float psi = bar * 14.504;
        
        switch(i) {
            case 0: tp.front_left_psi = psi; break;
            case 1: tp.front_right_psi = psi; break;
            case 2: tp.rear_left_psi = psi; break;
            case 3: tp.rear_right_psi = psi; break;
        }
    }
    
    return tp;
}

/******************************************************************************
 * TIRE TEMPERATURE (TPMS) - NEEDS VERIFICATION
 ******************************************************************************/

// CAN ID: 0x4A7 (Broadcast @ ~0.4Hz)
// DLC: 8 bytes
// Note: Initial formula gave unrealistic values - needs calibration
typedef struct {
    uint8_t front_left_raw;   // Store raw until verified
    uint8_t front_right_raw;
    uint8_t rear_left_raw;
    uint8_t rear_right_raw;
} tire_temp_t;

static inline tire_temp_t decode_tire_temp(const uint8_t* data) {
    tire_temp_t tt;
    
    // Store raw values - formula needs verification
    // Likely needs offset or different byte positions
    tt.front_left_raw = data[0];
    tt.front_right_raw = data[1];
    tt.rear_left_raw = data[2];
    tt.rear_right_raw = data[3];
    
    return tt;
}

/******************************************************************************
 * WHEEL SPEED - NEEDS VERIFICATION
 ******************************************************************************/

// CAN ID: 0x0B4 (Broadcast @ ~1.7Hz)
// DLC: 8 bytes
typedef struct {
    uint8_t raw_data[8];  // Store raw until verified
} wheel_speed_t;

static inline wheel_speed_t decode_wheel_speed(const uint8_t* data) {
    wheel_speed_t ws;
    for (int i = 0; i < 8; i++) {
        ws.raw_data[i] = data[i];
    }
    return ws;
}

/******************************************************************************
 * STEERING ANGLE - NEEDS VERIFICATION
 ******************************************************************************/

// CAN ID: 0x025 (Broadcast @ ~3.4Hz)
// DLC: 8 bytes
typedef struct {
    uint8_t raw_data[8];  // Store raw until verified
} steering_angle_t;

static inline steering_angle_t decode_steering_angle(const uint8_t* data) {
    steering_angle_t sa;
    for (int i = 0; i < 8; i++) {
        sa.raw_data[i] = data[i];
    }
    return sa;
}

/******************************************************************************
 * ENGINE & TRANSMISSION DATA - NEEDS VERIFICATION
 ******************************************************************************/

// CAN ID: 0x1C4 (Broadcast @ ~8.9Hz)
// DLC: 8 bytes
typedef struct {
    uint8_t raw_data[8];  // Store raw until verified
} engine_trans_t;

static inline engine_trans_t decode_engine_trans(const uint8_t* data) {
    engine_trans_t et;
    for (int i = 0; i < 8; i++) {
        et.raw_data[i] = data[i];
    }
    return et;
}

/******************************************************************************
 * CALIBRATION GUIDE
 ******************************************************************************/

/*
 * HOW TO VERIFY & CALIBRATE FORMULAS:
 * 
 * 1. TIRE PRESSURE (0x0AA) - ✅ VERIFIED
 *    - Formula: raw / 30 = kPa, then * 0.145038 = PSI
 *    - Test: Check with tire gauge, should match within 1-2 PSI
 * 
 * 2. TIRE TEMPERATURE (0x4A7) - ⚠️ NEEDS WORK
 *    - Capture data while noting dashboard temp display (if available)
 *    - Expected range: 20-50°C ambient, up to 70°C+ after driving
 *    - Current raw values don't decode correctly
 * 
 * 3. VEHICLE SPEED (0x024) - ⚠️ NEEDS WORK
 *    - Compare to GPS speed or speedometer
 *    - Bytes 4-5 appear to contain speed data
 *    - Initial formula gave 155 MPH in parking lot = wrong!
 * 
 * 4. WHEEL SPEED (0x0B4) - ⚠️ NEEDS WORK
 *    - Should match vehicle speed when not slipping
 *    - Useful for detecting tire slip/lockup
 * 
 * 5. STEERING ANGLE (0x025) - ⚠️ NEEDS WORK
 *    - Center should be 0 degrees
 *    - Positive = right turn, Negative = left turn
 *    - Turn wheel lock-to-lock while logging to find pattern
 * 
 * CALIBRATION PROCEDURE:
 * 
 * For each signal:
 * 1. Record CAN data while noting the dash/gauge reading
 * 2. Try different byte positions and formulas
 * 3. Look for patterns:
 *    - Values that change smoothly with sensor input
 *    - Ranges that make sense (e.g., 0-180°, 0-120 MPH)
 * 4. Test edge cases (0, max, middle values)
 * 
 * USEFUL TOOLS:
 * - Physical tire pressure gauge (for 0x0AA verification)
 * - GPS speed app (for speed calibration)
 * - Level/protractor (for steering angle)
 * - OBD-II scanner (for cross-reference)
 */

/******************************************************************************
 * EXAMPLE USAGE
 ******************************************************************************/

/*
void example_usage() {
    // Assuming you have a TWAI message
    twai_message_t msg;
    
    if (msg.identifier == 0x0AA && msg.data_length_code == 8) {
        tire_pressure_t tp = decode_tire_pressure(msg.data);
        
        printf("Tire Pressures:\n");
        printf("  FL: %.1f PSI\n", tp.front_left_psi);
        printf("  FR: %.1f PSI\n", tp.front_right_psi);
        printf("  RL: %.1f PSI\n", tp.rear_left_psi);
        printf("  RR: %.1f PSI\n", tp.rear_right_psi);
        
        // Alert on low pressure
        if (tp.front_left_psi < 30.0 || tp.front_right_psi < 30.0 ||
            tp.rear_left_psi < 30.0 || tp.rear_right_psi < 30.0) {
            printf("⚠️  LOW TIRE PRESSURE DETECTED!\n");
        }
    }
}
*/

#endif // TOYOTA_4RUNNER_CAN_DECODER_VERIFIED_H
