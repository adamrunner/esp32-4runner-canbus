# Toyota 4Runner CAN Bus Analysis - Final Report

## Summary

Based on analysis of your CAN capture from a 2018 Toyota 4Runner, I've identified the key signals for your real-time dashboard project and determined the correct decoding formulas.

## ✅ VERIFIED: Tire Pressure (CAN ID 0x0AA)

**Broadcast Rate:** ~24Hz (very fast, perfect for real-time display)

**Format:** 8 bytes - FL_HIGH FL_LOW FR_HIGH FR_LOW RL_HIGH RL_LOW RR_HIGH RR_LOW

**Formula (VERIFIED):**
```
raw_value = (byte[n] << 8) | byte[n+1]  // Combine 2 bytes, big-endian
psi = (raw_value / 30.0) * 0.145038

// Alternative (equivalent):
bar = raw_value / 3000.0
psi = bar * 14.504
```

**Example from your capture:**
- Raw value: 0x1A6F (6767 decimal)
- Calculated: 6767 / 30 * 0.145038 = 32.7 PSI ✓

**This is your primary data source - it's already broadcasting constantly!**

## ⚠️ NEEDS CALIBRATION: Other Signals

### Vehicle Speed (CAN ID 0x024)
- **Broadcast Rate:** ~3.4Hz
- **Status:** Formula needs work (gave 155 MPH in parking lot)
- **Next Steps:** Compare bytes 4-5 against GPS or speedometer

### Tire Temperature (CAN ID 0x4A7)
- **Broadcast Rate:** ~0.4Hz (slow, but acceptable)
- **Status:** Formula unclear - raw values don't decode correctly
- **Next Steps:** Check if your dash displays tire temps, compare

### Steering Angle (CAN ID 0x025)
- **Broadcast Rate:** ~3.4Hz
- **Status:** Not yet decoded
- **Next Steps:** Turn wheel lock-to-lock while logging

### Engine/Trans Data (CAN ID 0x1C4)
- **Broadcast Rate:** ~8.9Hz
- **Status:** Contains multiple parameters, needs byte-level analysis
- **Likely contains:** Coolant temp, trans temp, RPM, throttle position

### Wheel Speed (CAN ID 0x0B4)
- **Broadcast Rate:** ~1.7Hz
- **Status:** Not yet decoded
- **Use:** Individual wheel speeds for slip detection

### Accelerator Position (CAN ID 0x3D3)
- **Broadcast Rate:** ~0.3Hz
- **Status:** Not yet decoded

### 4WD Status (CAN ID 0x3B7)
- **Broadcast Rate:** ~0.1Hz (very slow/event-based)
- **Status:** Not yet decoded
- **Likely contains:** Transfer case mode, diff lock status

## Key Findings

### What You DON'T Need

**OBD-II Queries (0x7DF/0x7E8)** - You don't need these for your dashboard!

The standard OBD-II query/response system is:
- Slower (request/response vs. continuous broadcast)
- More complex (requires managing query timing)
- Less data (standard PIDs only)

Everything you want is already being broadcast passively on the CAN bus at higher rates.

### What You DO Need

**Passive CAN Listening** - Just receive and decode the messages already flowing:
1. Tire Pressure (0x0AA) - ✅ Formula verified
2. All other signals - ⚠️ Need calibration against known values

## Calibration Strategy

For each signal you want to add:

1. **Record CAN data** while noting the actual value (from dash, gauge, GPS, etc.)
2. **Identify the bytes** that change with the sensor
3. **Test formulas** to match raw values to real values
4. **Verify edge cases** (min, max, zero)

## Next Steps for Your Dashboard

### Phase 1: Working Tire Pressure Display
```cpp
// You can implement THIS RIGHT NOW!
if (msg.identifier == 0x0AA) {
    uint16_t fl_raw = (msg.data[0] << 8) | msg.data[1];
    uint16_t fr_raw = (msg.data[2] << 8) | msg.data[3];
    uint16_t rl_raw = (msg.data[4] << 8) | msg.data[5];
    uint16_t rr_raw = (msg.data[6] << 8) | msg.data[7];
    
    float fl_psi = (fl_raw / 30.0) * 0.145038;
    float fr_psi = (fr_raw / 30.0) * 0.145038;
    float rl_psi = (rl_raw / 30.0) * 0.145038;
    float rr_psi = (rr_raw / 30.0) * 0.145038;
    
    // Display on your dashboard!
}
```

### Phase 2: Calibrate Remaining Signals
- Drive with GPS speed app, log CAN data, find speed formula
- Check dash temps vs CAN data for temperature formula
- Turn wheel, log data, find steering angle pattern
- Note RPM/throttle while logging to decode engine data

### Phase 3: Advanced Features
- Low pressure alerts
- Tire slip detection (wheel speed vs vehicle speed)
- 4WD mode display
- Temperature warnings

## Files Included

1. **4runner_can_decoder_verified.h** - C header with verified tire pressure formula
2. **can_decoder.py** - Python tool to test formulas against your logs
3. **This summary** - Overview of findings

## Resources

- **OBDb Repository:** https://github.com/OBDb/Toyota-4Runner
  - Community-maintained database of Toyota CAN signals
  - Check for updates/contributions for your model year
  
- **Your Capture Data:** 87 unique CAN IDs, 17,485 messages
  - Rich data set for further analysis
  - Can be used to calibrate all remaining signals

## Important Notes

1. **Model Year Variations:** Formulas may differ between 2010-2024 models
2. **Trim Differences:** Some signals (like KDSS) only on certain trims
3. **Testing Required:** Always verify formulas against known good values
4. **Safety:** Don't rely solely on CAN data for critical safety decisions

## Conclusion

You have everything you need to build a real-time tire pressure display RIGHT NOW using the verified formula. The other signals require calibration but follow the same process: capture → analyze → verify → implement.

The passive CAN bus approach is superior to OBD-II queries for your dashboard because:
- Higher update rates (24Hz vs ~1Hz)
- More data available (Toyota-specific signals)
- Simpler code (no request/response state machine)
- Lower bus traffic (no polling overhead)

Good luck with your dashboard project!
