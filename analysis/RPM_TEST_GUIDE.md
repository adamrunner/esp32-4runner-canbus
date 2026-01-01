# RPM Broadcast Testing (CAN ID 0x024)

## Overview

Added real-time RPM testing display to compare broadcast decoding with diagnostic values.

## Changes Made

### 1. Broadcast RPM Decoder Function

Added `handle_broadcast_rpm_test()` function to decode CAN ID 0x024 messages with 4 different hypotheses:

**Test 1**: Bytes 4-5 big-endian / 32
- Formula: `rpm = (b4 << 8 | b5) / 32`

**Test 2**: Bytes 4-5 big-endian / 64
- Formula: `rpm = (b4 << 8 | b5) / 64`

**Test 3**: Bytes 6-7 big-endian / 32
- Formula: `rpm = (b6 << 8 | b7) / 32`

**Test 4**: Bytes 6-7 big-endian / 64
- Formula: `rpm = (b6 << 8 | b7) / 64`

### 2. Metrics Structure Updates

Added to `can_metrics_t` struct:
```cpp
// Broadcast RPM test decodings (from 0x024)
float bcast_rpm_1;
float bcast_rpm_2;
float bcast_rpm_3;
float bcast_rpm_4;
bool bcast_rpm_valid;
```

### 3. New Display Page - "RPM Test"

Created dedicated page 6 to compare:
- **Diag RPM**: Diagnostic value from OBD PID 0x0C (reference)
- **Test 1 (b4-5/32)**: First broadcast hypothesis
- **Test 2 (b4-5/64)**: Second broadcast hypothesis
- **Test 3 (b6-7/32)**: Third broadcast hypothesis
- **Test 4 (b6-7/64)**: Fourth broadcast hypothesis

### 4. CAN Message Processing

Updated `process_obd_response()` to handle CAN ID 0x024 broadcasts:
```cpp
// Handle broadcast RPM test (0x024)
if (msg->identifier == RPM_TEST_BROADCAST_ID) {
    handle_broadcast_rpm_test(msg);
    return;
}
```

## Testing Instructions

### Step 1: Build and Flash

```bash
. /Users/adamrunner/esp/v5.5/esp-idf/export.sh
idf.py build flash monitor
```

### Step 2: Navigate to RPM Test Page

- Power on ESP32 and 4Runner
- Swipe through pages to reach "RPM Test" page (page 6 of 6)
- Wait for CAN messages to populate values

### Step 3: Compare Values While Driving

For each test, compare with diagnostic RPM:

1. **Find the match**: Look for which test value closely tracks the diagnostic RPM
2. **Check update rate**: Broadcast values should update faster than diagnostic (every ~150ms)
3. **Validate range**: Values should be 0-8000 RPM for normal driving
4. **Test scenarios**:
   - Idle (~600-1000 RPM)
   - Cruising (~2000-3000 RPM)
   - Accelerating (~3000-5000 RPM)
   - Decelerating (RPM drops)

### Step 4: Identify Correct Decoding

The correct hypothesis will show:
- ✓ Values consistently matching diagnostic RPM (within reasonable tolerance)
- ✓ No unrealistic values (negative, >8000 RPM when idling)
- ✓ Smooth updates (no erratic jumps)
- ✓ Correct trend behavior (increases during acceleration, decreases during deceleration)

## Expected Outcomes

### If bytes 4-5 are RPM:

- **Test 1 (div/32)**: Correct if matches diagnostic
- **Test 2 (div/64)**: Correct if half of diagnostic value

### If bytes 6-7 are RPM:

- **Test 3 (div/32)**: Correct if matches diagnostic
- **Test 4 (div/64)**: Correct if half of diagnostic value

### Alternative Decodings (if none match):

If none of the 4 hypotheses work, we may need:
- Different byte pairs (0-1, 2-3)
- Different division factors (16, 128, 256)
- Offset subtraction (like wheel speeds)
- 32-bit value instead of 16-bit

## Log File Analysis

After testing, capture CAN log with RPM test page visible:

```bash
# Start logging on "Logging" page
# Drive through various RPM ranges (idle, cruise, accelerate, decelerate)
# Stop logging
```

Then analyze with:

```bash
# Export RPM test data from log
# (Create analysis script to extract 0x024 messages and decode with various formulas)
# Correlate with diagnostic RPM (0x7E8 responses to PID 0x0C)
```

## Why These Hypotheses?

Based on CAN analysis of `logs/CAN_0001.CSV` and `logs/CAN_0002.CSV`:

1. **Frequency**: 46-65 Hz matches typical engine RPM sensor rates (50-100 Hz)
2. **Variation**: 63.6% data variation suggests dynamic sensor data
3. **Sample data**:
   - `01 FF 02 0B 61 F8 80 12`
   - Bytes 4-5: `0x61F8` = 25,080
     - /32 = 784 RPM ✓ (plausible idle/cruise)
     - /64 = 392 RPM (too low for idle)
   - Bytes 6-7: `0x8012` = 32,786
     - /32 = 1,025 RPM ✓ (plausible for driving)
     - /64 = 514 RPM (possible for idle)

## Next Steps After Testing

### If Correct Decoding Found:

1. **Update code**: Remove test hypotheses, implement correct decoding
2. **Add to broadcast**: Update `WHEEL_SPEED_BROADCAST_ID` comments to include RPM
3. **Document**: Add to decoding documentation

### If No Match:

1. **Expand hypotheses**: Test other byte pairs and division factors
2. **Check for offset**: Try subtracting offset (like 6750 for wheel speeds)
3. **Analyze correlation**: Use Python tools to find statistical correlation
4. **Log during events**: Capture more logs with varying RPM conditions

### Alternative CAN IDs:

If 0x024 isn't RPM, test:
- **0x2C1**: High variation (86.8%), likely transmission/engine data
- **0x0B4**: High variation (88.6%), possible throttle/sensor
- **0x1D0**: Moderate variation, possible gear/status

## Files Modified

- `main/4runner_canbus_main.cpp`:
  - Added `handle_broadcast_rpm_test()` function
  - Updated `can_metrics_t` struct with broadcast RPM fields
  - Created `rpm_page_create()` and related functions
  - Updated `process_obd_response()` to handle 0x024
  - Registered RPM page in `app_main()`
  - Updated page counters (4/4 → 4/6, 5/5 → 6/6)

## Validation Checklist

- [ ] Build succeeds without errors
- [ ] Flash successfully
- [ ] RPM Test page appears
- [ ] Diagnostic RPM displays correctly
- [ ] Broadcast RPM values populate
- [ ] One or more test values track diagnostic RPM
- [ ] Values are reasonable (0-8000 RPM range)
- [ ] No erratic or invalid values displayed

---

## Quick Reference: Diagnostic RPM

The diagnostic RPM comes from OBD PID 0x0C:
- **Request**: 0x7E0 → `[02 01 0C]` (length=2, service=0x01, PID=0x0C)
- **Response**: 0x7E8 → `[04 41 0C XX YY]` (length=4, service=0x41, PID=0x0C, RPM_HI=RPM_LO)
- **Decoding**: `RPM = (XX << 8 | YY) / 4.0f`
- **Example**: `0F A0` = 4000 / 4.0 = 1000 RPM

This is your **ground truth** for validating broadcast decodings!
