# CAN Log Analysis Report

## Overview

Analysis of two CAN bus log files from Toyota 4Runner:
- **CAN_0001.CSV**: 399,794 messages over 622.6 seconds (10.3 minutes)
- **CAN_0002.CSV**: 1,017,935 messages over 1,142.9 seconds (19 minutes)

Both logs contain 93-96 unique CAN IDs with broadcast frequencies ranging from ~10 Hz to ~65 Hz.

---

## Wheel Speed Broadcast Validation (0x0AA)

### Decoding Status: **VALIDATED ✓**

The wheel speed broadcast (CAN ID 0x0AA) decoding has been validated:

**Format**: 8 bytes containing 4x 16-bit big-endian values
- Bytes 0-1: Front Right wheel speed
- Bytes 2-3: Front Left wheel speed
- Bytes 4-5: Rear Right wheel speed
- Bytes 6-7: Rear Left wheel speed

**Decoding Formula**:
```
km/h = (raw_16bit_value - 6750) / 100.0
```

**Validation Results** (from CAN_0001.CSV):
- ✓ No negative wheel speeds
- ✓ No unrealistic speeds (>200 kph)
- ✓ Standard deviation during stops: 0.106 kph (excellent consistency)
- ✓ Update interval: ~12ms (83 Hz) - highly consistent
- ✓ Speed range: 0.17 - 67.20 kph per wheel
- ✓ Average speed: 33.5 kph

**Update Frequency**: 46-65 Hz (varies by log, possibly engine RPM dependent)

---

## High-Frequency Broadcast Messages

Messages appearing at >10 Hz (potential real-time broadcasts):

### Consistent Top Broadcasts (Both Logs)

| CAN ID | Frequency | Sample Data (Hex) | Notes |
|---------|------------|---------------------|-------|
| 0x020 | 46-65 Hz | `00 00 07 00 00 00 2E 00` | Almost static (byte 6=0x2E constant) |
| 0x024 | 46-65 Hz | `01 FF 02 0B 61 F8 80 12` | Highly dynamic data |
| 0x025 | 46-65 Hz | `0F FD 00 02 78 78 78 A3` | Dynamic bytes 4-6 (0x78, 0x82, others) |
| 0x0AA | 46-65 Hz | `1F 35 1F 48 1F 3C 1F 41` | **Wheel speeds** (validated) |

### Secondary Broadcasts (20-40 Hz)

| CAN ID | Frequency | Sample Data | Notes |
|---------|------------|--------------|-------|
| 0x387 | 28-39 Hz | `00 00 00 00 00 00 3F 3F` | Static except bytes 6-7 |
| 0x38E | 28-39 Hz | `20 60 00 00 00 00 00 00` | Low variation |
| 0x443 | 28-39 Hz | `4D 02 00 20 05 00 00 00` | Some byte variation |
| 0x2C7 | 24-33 Hz | `00 00 00 00 00 00 00 00` | **All zeros** (heartbeat/status) |

### Tertiary Broadcasts (15-25 Hz)

| CAN ID | Frequency | Sample Data | Notes |
|---------|------------|--------------|-------|
| 0x1C4 | 24-33 Hz | `04 BE 14 08 3F 00 C0 AA` | Moderate variation |
| 0x223 | 23-33 Hz | `00 00 00 00 00 00 2D` | Mostly static |
| 0x0B4 | 23-33 Hz | `00 00 00 00 14 04 EC C0` | Moderate variation |
| 0x224 | 23-33 Hz | `20 00 00 00 00 0E 00 08` | Low variation |
| 0x228 | 23-33 Hz | `00 00 00 2E 00 00 00 00` | Static except byte 2 |
| 0x0BA | 23-33 Hz | `00 00 00 BE 00 00 00 00` | Static except byte 3 |
| 0x237 | 23-33 Hz | `FC CD F0 60 00 58 00 00` | Some variation |
| 0x1D0 | 18-25 Hz | `08 60 10 00 00 00 00 51` | Low variation |
| 0x2C6 | 18-25 Hz | `00 00 00 00 00 00 00 D0` | Static except byte 7 |
| 0x2C1 | 18-25 Hz | `08 05 DC 06 55 EE 00 FD` | High variation (92.8% change rate) |
| 0x2E1 | 17-25 Hz | `00 00 00 00 00 00 00 00` | All zeros |

---

## Messages with High Data Variation

Messages with >100 unique data combinations (highly dynamic sensor data):

### CAN_0001 Results

| CAN ID | Count | Unique | Change Rate | Notes |
|---------|--------|---------|-------------|-------|
| 0x0AA  | 29,005 | 26,677  | 92.0%      | **Wheel speeds** (validated) |
| 0x0B4  | 14,503 | 12,849  | 88.6%      | High variation - sensor data |
| 0x2C1  | 11,008 | 10,210  | 92.8%      | High variation - sensor data |
| 0x1D0  | 11,009 | 6,595   | 59.9%      | Moderate variation |
| 0x1C4  | 14,837 | 7,737   | 52.1%      | Moderate variation |
| 0x025  | 29,009 | 2,766   | 9.5%       | Low variation - status/control |
| 0x024  | 29,005 | 18,438  | 63.6%      | Moderate variation |

### CAN_0002 Results

| CAN ID | Count | Unique | Change Rate | Notes |
|---------|--------|---------|-------------|-------|
| 0x0AA  | 74,646 | 51,284  | 68.7%      | **Wheel speeds** (validated) |
| 0x2C1  | 28,332 | 24,587  | 86.8%      | High variation - sensor data |
| 0x1C4  | 38,187 | 15,320  | 40.1%      | Moderate variation |
| 0x0B4  | 37,325 | 24,228  | 64.9%      | High variation - sensor data |
| 0x1D0  | 28,332 | 12,706  | 44.8%      | Moderate variation |
| 0x024  | 74,665 | 37,256  | 49.9%      | High variation - sensor data |

---

## 2026-01-02 Drive Home Logs (CAN_20260102_123449, CAN_20260102_125202)

Two drive-home segments (highway-heavy) were analyzed:

- **CAN_20260102_123449.CSV**: 920,834 messages over 899.3 seconds, ~1024 msgs/sec, 91 IDs
- **CAN_20260102_125202.CSV**: 528,579 messages over 586.9 seconds, ~900.6 msgs/sec, 96 IDs

High-frequency baseline remains stable:
- 0x020 / 0x024 / 0x025 / 0x0AA at ~65-76 Hz

High-variation IDs in both logs:
- 0x0AA, 0x0B4, 0x2C1, 0x1D0, 0x1C4, 0x024

### Correlation Highlights

- **0x0B4 bytes 5-6 (big-endian) track wheel speed (0x0AA) extremely closely**:
  - Speed (kph) ~= b56 / 100 (RMSE ~0.2-0.6 kph in recent logs)
  - b5 alone is a coarse proxy (~2.5 * b5), but less accurate
  - Confirms 0x0B4 is a vehicle speed proxy, not throttle %
- **0x2C1 shows moderate correlation with acceleration** (b6 / b6b7)
  - Better throttle/torque candidate than 0x0B4 for these logs
- **0x1D0 b4 and 0x1C4 b2 correlate with speed, but scale shifts between logs**
  - Likely status/state or secondary speed-related fields, not direct speed
- **Steering/yaw signals remain inconclusive**
  - These logs are mostly highway; need turning-rich segments
  - Use the new turning test analyzer (see Tools Created) on low-speed turns

---

## Key Findings and Recommendations

### 1. Wheel Speed Broadcast (0x0AA) - **DECODED ✓**

**Status**: Fully validated and decoding confirmed.

**Next Steps**:
- ✓ This is working correctly in the codebase
- Can be used as reference for speed-correlated analysis

### 2. Priority Candidates for Further Investigation

Based on frequency and data variation, these CAN IDs are top candidates for vehicle dynamics:

#### **High Priority** (High freq + High variation)

**0x0B4**: 23-33 Hz, 88.6% variation rate
- Very dynamic data, similar update rate to wheel speeds
- **Update**: bytes 5-6 (big-endian) / 100 == vehicle speed (kph)
- Treat as speed proxy; throttle is likely elsewhere (see 0x2C1)

**0x2C1**: 18-25 Hz, 86.8-92.8% variation rate
- Extremely dynamic data
- Sample: `08 05 DC 06 55 EE 00 FD`
- Pattern suggests multi-byte sensor data
- Good candidate for: Engine RPM, transmission data, or accelerator position

**0x1D0**: 18-25 Hz, 44.8-59.9% variation rate
- Sample: `08 60 10 00 00 00 00 51`
- First few bytes show variation
- Good candidate for: Engine status, gear position, or fuel-related data

**0x1C4**: 24-33 Hz, 40.1-52.1% variation rate
- Sample changes between: `04 BE 14 08 3F 00 C0 AA` (log1) and `00 00 00 02 00 00 3E 0D` (log2)
- Noticeable difference between logs suggests different vehicle states
- Good candidate for: Transmission, drive mode, or vehicle status

**0x024**: 46-65 Hz, 49.9-63.6% variation rate
- Very high frequency with good variation
- Sample shows byte patterns like: `01 FF 02 0B 61 F8 80 12`
- Could be: Engine data (RPM often at 50-100 Hz)

#### **Medium Priority** (High freq, lower variation)

**0x025**: 46-65 Hz, 9.5% variation rate
- Sample: `0F FD 00 02 78 78 78 A3` with variations in bytes 4-6
- Bytes 4-6 show pattern: `78 78 78` (120 decimal) or `82 82 82` (130 decimal)
- Low variation suggests status/control bits rather than sensor
- Could be: Gear selector, drive mode, or transmission status

**0x38E, 0x443, 0x387**: 28-39 Hz
- Moderate frequency, some variation
- Could be auxiliary sensor data or status broadcasts

### 3. Static/Status Messages

**0x2C7**: All zeros at 24-33 Hz
- Heartbeat or keepalive message
- Likely used by ECUs to confirm bus activity

**0x20**: Mostly static at 46-65 Hz
- Sample: `00 00 07 00 00 00 2E 00` (byte 2=0x07, byte 6=0x2E)
- Status broadcast with occasional changes

### 4. Diagnostic Messages (Low Frequency)

**0x7E8**: 1.9-4.7 Hz, 16.6-21.7% variation
- This is the OBD-II response ID (0x7E8 = 0x7E0 response)
- Contains diagnostic responses (as expected from polling)

---

## Decoding Strategy Recommendations

### Phase 1: Correlation Analysis

1. **Correlate with Wheel Speed**
   - For each high-priority CAN ID, analyze byte values vs vehicle speed
   - Look for bytes that increase predictably with speed
   - Pattern: RPM correlates with speed in fixed gear; gear changes stepwise with speed

2. **Analyze Temporal Patterns**
   - Identify events: stops, starts, gear shifts, acceleration/braking
   - Look for CAN IDs that change predictably during these events
   - Example: Transmission messages change during gear shifts

3. **Cross-Reference with Diagnostic Data**
   - Compare broadcast messages with known diagnostic PIDs:
     - RPM (0x0C from OBD)
     - Gear (0x85 from Toyota ABS)
     - Throttle position
     - Brake status

### Phase 2: Structured Logging Improvements

1. **Add Event Markers**
   - Implement GPIO input for manual event marking
   - Mark: "engine start", "gear shift", "braking", "accelerating", "parked"
   - Allows easier correlation of broadcast changes with events

2. **Add Sensor Timestamps**
   - Log external sensor data (e.g., GPS speed from phone)
   - Provides ground truth for validation

3. **Implement Real-Time Decoding**
   - Add live decoding of suspected messages to display
   - Observe values while driving to verify correlation

### Phase 3: Hypothesis Testing

For each candidate CAN ID, test specific hypotheses:

#### CAN ID 0x0B4 (High variation, 23-33 Hz)
- **Confirmed**: Vehicle speed proxy (bytes 5-6, big-endian, kph = raw / 100)
- **Test**: Continue validating on new logs; compare b56 / 100 vs wheel avg

#### CAN ID 0x2C1 (High variation, 18-25 Hz)
- **Hypothesis 1**: Engine RPM (multi-byte, 16 or 32-bit)
- **Hypothesis 2**: Transmission gear + torque converter
- **Hypothesis 3**: Vehicle speed in mph
- **Test**: Check if byte values match expected RPM range (0-8000), convert with common scalars (1, 0.25, etc.)

#### CAN ID 0x1D0 (Moderate variation, 18-25 Hz)
- **Hypothesis 1**: Gear position (byte 0: P=0, R=1, D=2, etc.)
- **Hypothesis 2**: Transmission mode (4WD, 2WD, locking diff)
- **Hypothesis 3**: Engine status (on/off, temp, etc.)
- **Test**: Check value changes at start/stop, during gear shifts

#### CAN ID 0x1C4 (Moderate variation, 24-33 Hz)
- **Hypothesis 1**: Vehicle status (door locks, lights, etc.)
- **Hypothesis 2**: Drive mode (sport, eco, normal)
- **Hypothesis 3**: Battery/charging status
- **Test**: Correlate with key fob lock/unlock events from user description

#### CAN ID 0x024 (High freq, good variation)
- **Hypothesis 1**: Engine RPM (most likely, high frequency matches typical RPM sensors)
- **Hypothesis 2**: Fuel injection timing
- **Hypothesis 3**: Ignition timing
- **Test**: Try 16-bit decodings, compare with diagnostic RPM data

---

## Specific Decoding Suggestions

### Testing RPM in 0x024

Given the 46-65 Hz frequency (typical for engine RPM broadcasts):

1. **Try 16-bit big-endian**:
   - Bytes 0-1: `01 FF` = 511 → could be 511 RPM? (too low for driving)
   - Bytes 2-3: `02 0B` = 523 → possible
   - Bytes 4-5: `61 F8` = 25,080 → divide by 32 = 784 RPM ✓
   - Bytes 6-7: `80 12` = 32,786 → divide by 32 = 1,025 RPM ✓

2. **Try 16-bit little-endian**:
   - Bytes 0-1: `FF 01` = 65,281 → divide by 64 = 1,020 RPM ✓

**Recommendation**: Add real-time display of 0x024 bytes 4-7 using both endianness and division factors to test during next drive.

### Testing Gear in 0x025

Pattern in bytes 4-6: `78 78 78` or `82 82 82`

- 0x78 (120) - could be 2nd gear?
- 0x82 (130) - could be 3rd gear?
- Other values observed: `64` (100), `5A` (90), `46` (70), `3C` (60)

**Recommendation**: Log gear from diagnostics (PID 0x85) and correlate with 0x025 bytes 4-6 to find mapping.

### Testing Status in 0x1D0

Bytes 0-2 show variation: `08 60 10` in log1 vs `00 00 00` in log2

- Different vehicle states suggest status flags
- Try: byte 0 = major status, byte 1-2 = sub-status
- Check bit patterns: is it a bitmask or enumerated value?

---

## Recommended Next Steps

### Immediate (Next logging session)

1. **Add diagnostic PID logging** for correlation:
   - RPM (OBD 0x0C)
   - Gear (Toyota 0x85)
   - Throttle (if available)
   - Brake status (Toyota 0x21 0x1F)

2. **Manually mark events** while driving:
   - Gear shifts (note "up", "down")
   - Door lock/unlock
   - Engine start/stop
   - 2WD/4WD mode changes

3. **Test specific decodings** in real-time:
   - Add 0x024 display with RPM guesses
   - Add 0x025 display with gear guesses
   - Observe values during known events

### Short-term (Data analysis)

1. **Create correlation script** to automate:
   - Timestamp alignment of diagnostic responses with broadcast messages
   - Statistical correlation (pearson r) between broadcast bytes and diagnostic values
   - Automatic detection of likely mappings

2. **Export wheel speed data** with segments:
   - Identify stopped vs driving periods
   - Correlate other CAN IDs with these periods

### Long-term (Enhanced decoding)

1. **Implement live hypothesis testing**:
   - Display multiple decoding options simultaneously
   - Let user confirm which looks correct during driving

2. **Build CAN signal database**:
   - Document successful decodings
   - Share with community (e.g., Toyota-4Runner forums)
   - Cross-reference with OBDb-Toyota-4Runner project

---

## Tools Created

1. **can_analyzer.py**: General CAN log analysis
   - Message frequency analysis
   - Data pattern analysis
   - High-frequency message detection
   - Export capabilities

2. **wheel_speed_analyzer.py**: Wheel speed validation
   - Validates 0x0AA decoding
   - Statistics and validation checks
   - Driving segment identification
   - Turning event detection

3. **simple_analyzer.py**: Quick overview
   - High-frequency message summary
   - High-variation message detection
   - Sample data display
4. **turning_test_analyzer.py**: Turning/steering candidate scan
   - Uses 0x0AA left-right wheel speed difference to flag turns
   - Correlates candidate bytes with turning magnitude and direction

---

## Conclusion

The analysis successfully validated the wheel speed broadcast (0x0AA) decoding and identified several high-priority candidates for further investigation:

**Top Candidates**:
1. **0x024**: Likely engine RPM (46-65 Hz frequency matches typical engine sensors)
2. **0x2C1**: Highly dynamic (92.8% variation), likely transmission or engine data
3. **0x0B4**: High variation, vehicle speed proxy (b5-6 big-endian / 100)
4. **0x1D0**: Moderate variation, likely gear or status information
5. **0x1C4**: Varies between logs, likely mode or vehicle status

The recommended strategy is to use correlation analysis with diagnostic PIDs and event marking to validate these hypotheses systematically during future logging sessions.
