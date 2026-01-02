# Quick Reference: Next Logging Session

## Priority: Test Top Candidate Messages

### 1. Engine RPM - CAN ID 0x024 (46-65 Hz)

**To Test:**
- Try decoding bytes 4-7 as 16-bit values:
  - Big-endian: `(b4 << 8) | b5`
  - Little-endian: `(b5 << 8) | b4`
- Try division factors: 32, 64, 128
- Expected RPM range: 0-8000 RPM

**Display Idea:**
```
Bytes 4-5 (big-endian):
Raw: 0x61F8 = 25,080
/32  = 784 RPM  ← Likely
/64  = 392 RPM

Bytes 6-7 (big-endian):
Raw: 0x8012 = 32,786
/32  = 1,025 RPM  ← Possible
```

### 2. Gear Position - CAN ID 0x025 (46-65 Hz)

**To Test:**
- Monitor bytes 4-6: `78 78 78` or `82 82 82`
- Values observed: 0x3C (60), 0x46 (70), 0x5A (90), 0x64 (100), 0x78 (120), 0x82 (130)
- Compare with diagnostic gear (PID 0x85)

**Hypothesis:**
```
0x3C (60) = Park?
0x64 (100) = Drive?
0x78 (120) = 2nd gear?
0x82 (130) = 3rd gear?
```

### 3. Speed Proxy / Sensor - CAN ID 0x0B4 (23-33 Hz, 88.6% variation)

**To Test:**
- **Update**: byte 5 correlates almost perfectly with wheel speed (kph)
- Treat 0x0B4 as a vehicle speed proxy; throttle likely elsewhere (0x2C1?)
- Verify mapping on new logs (speed ~= 2.5 * b5 + 0.6)

### 4. Transmission/Status - CAN ID 0x2C1 (18-25 Hz, 86.8% variation)

**To Test:**
- Very dynamic - likely multi-byte data
- Try 16-bit encodings for various byte pairs
- Monitor during gear shifts

### 5. Status/Mode - CAN ID 0x1D0 (18-25 Hz, 44.8% variation)

**To Test:**
- Different values between logs suggests vehicle states
- Check bit flags vs enumerated values
- Correlate with drive mode (4WD vs 2WD, sport mode, etc.)

---

## Quick Test Commands

### Before Driving (Setup ESP32)
```bash
# Build and flash with current code
. /Users/adamrunner/esp/v5.5/esp-idf/export.sh
idf.py build flash monitor
```

### During Driving (Note Events)
Make mental or physical notes for each event:
- Engine start/stop
- Gear shifts (up/down, which gear)
- Mode changes (4WD, sport, eco)
- Acceleration events
- Braking events
- Lock/unlock with key fob
- Door open/close

### After Logging (Quick Analysis)
```bash
# Quick overview
python3 analysis/simple_analyzer.py logs/NEW_LOG.CSV

# Analyze specific candidate
python3 analysis/can_analyzer.py logs/NEW_LOG.CSV --analyze 024

# Validate wheel speeds
python3 analysis/wheel_speed_analyzer.py logs/NEW_LOG.CSV --validate --stats

# Turning test (best with low-speed turns / parking lot)
python3 analysis/turning_test_analyzer.py logs/NEW_LOG.CSV --top 5
```

---

## Real-Time Testing Strategy

### Add to Display (for next session)

1. **RPM Test Page** (for 0x024):
   - Display bytes 4-7 raw values
   - Display decoded values with different scalars
   - Update at ~50 Hz to match message rate

2. **Gear Test Page** (for 0x025):
   - Display byte 4-6 raw values
   - Display decoded gear guess (1-6, R, P, N, D)
   - Add gear from diagnostics for comparison

3. **Raw Data Page** (for 0x0B4, 0x1D0, 0x2C1):
   - Show raw hex and decimal values
   - Group by CAN ID
   - Update continuously

### Validation Checklist

For each candidate CAN ID:
- [ ] Does value change when expected?
- [ ] Does value stay constant when expected?
- [ ] Does value range make sense (e.g., 0-8000 RPM)?
- [ ] Does update rate match expectations (10-100 Hz for engine)?
- [ ] Can we find correlation with diagnostic PID?

---

## Correlation with Diagnostic PIDs

### Already Implemented (in codebase):
- ✓ Wheel speeds (0x0AA broadcast vs 0x7B0 PID 0x03)
- ✓ RPM (0x0C from OBD)
- ✓ Gear (0x85 from Toyota)
- ✓ ATF temperature (0x82 from Toyota)
- ✓ Fuel level (0x29 from Meter)
- ✓ Vehicle speed (0x21 from ABS)

### To Add for Next Session:
- **Throttle position** (if available in OBDb)
- **Brake status** (Toyota 0x21 PID 0x1F - brake light, parking brake)
- **Transmission mode** (check OBDb for available PIDs)
- **Drive mode** (4WD/AWD status)

---

## Expected Message Update Rates

Typical rates for reference (to validate findings):

| Data Type | Expected Rate |
|-----------|----------------|
| Wheel speeds | 80-100 Hz |
| Engine RPM | 50-100 Hz |
| Gear position | 10-50 Hz |
| Throttle | 20-100 Hz |
| Transmission status | 5-20 Hz |
| Vehicle status | 5-20 Hz |
| Door/lock status | 1-10 Hz |

If a message updates at expected rate and shows reasonable values, it's likely correctly decoded.

---

## Success Criteria

A broadcast message is successfully decoded when:

1. **Values make physical sense** (e.g., 0-8000 RPM, gears 1-6, speed 0-200 kph)
2. **Update rate matches expectations** (engine data ~50-100 Hz, status ~5-20 Hz)
3. **Correlates with behavior** (value changes during expected events)
4. **Matches diagnostic data** (when diagnostic available for comparison)

---

## File Locations

**Analysis tools:**
- `analysis/simple_analyzer.py` - Quick message overview
- `analysis/can_analyzer.py` - Detailed analysis
- `analysis/wheel_speed_analyzer.py` - Wheel speed validation

**Documentation:**
- `analysis/README.md` - Complete tool documentation
- `analysis/CAN_ANALYSIS_REPORT.md` - Detailed findings and strategy

**Logs:**
- `logs/CAN_0001.CSV` - First log (10.3 minutes)
- `logs/CAN_0002.CSV` - Second log (19 minutes)
- `analysis/can_0001_summary.csv` - Message frequency summary

---

## Quick Tip

Use the simple analyzer immediately after logging:
```bash
python3 analysis/simple_analyzer.py logs/NEW_LOG.CSV
```

This will show you:
- High-frequency messages (likely engine/sensor data)
- High-variation messages (dynamic sensor data)
- Sample data for each

Focus your investigation on messages that appear in both categories!
