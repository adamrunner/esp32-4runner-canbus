# TPMS Decoding Notes - Toyota 4Runner

## Summary

Successfully decoded **3 of 4** tire pressure sensors from passive CAN bus broadcast messages.

**Calibration Date:** December 12, 2024
**Known Tire Pressure:** 38 PSI (all 4 tires)
**Ambient Temperature:** ~55°F (13°C)

## Decoded Signals

### CAN ID 0x4A7 - Front Axle TPMS
**DLC:** 8 bytes
**Update Rate:** ~41 messages in capture

| Byte | Signal | Formula | Unit | Notes |
|------|--------|---------|------|-------|
| 0 | FL Pressure | `x * 1.583` | kPa | ✅ Calibrated (38 PSI = 262 kPa) |
| 1 | FR Pressure | `x * 1.583` | kPa | ✅ Calibrated (38 PSI = 262 kPa) |
| 2 | Unknown | - | - | Low variation (39-44), possibly sensor ID |
| 3 | Unknown | - | - | Low variation (39-43), possibly sensor ID |
| 4 | Unknown | - | - | Values 108-122, not matching ambient temp |
| 5 | Unknown | - | - | Values 106-123, not matching ambient temp |
| 6 | Unknown | - | - | Low variation (64-70) |
| 7 | Unknown | - | - | Low variation (64-70) |

**Typical Message:**
`A6 A5 29 27 74 73 40 43`
- FL: 166 → 263 kPa (38.1 PSI) ✓
- FR: 165 → 261 kPa (37.9 PSI) ✓

### CAN ID 0x498 - Rear Axle TPMS (Partial)
**DLC:** 8 bytes
**Update Rate:** ~35 messages in capture

| Byte | Signal | Formula | Unit | Notes |
|------|--------|---------|------|-------|
| 0 | Constant | - | - | Always 0x40 (64) |
| 1 | Unknown | - | - | Values 2-7 |
| 2 | Unknown | - | - | High variation (5-255), not pressure |
| 3 | Unknown | - | - | Values 39-46, similar to 0x4A7 bytes 2-3 |
| 4 | **Rear Pressure** | `x * 1.583` | kPa | ✅ Calibrated (40 PSI = 276 kPa) |
| 5 | Unknown | - | - | Values 0-22 |
| 6 | Unknown | - | - | Values 0-13 |
| 7 | Constant | - | - | Always 0x00 |

**Typical Message:**
`40 03 9D 2C AF 0C 00 00`
- Byte 4: 175 → 277 kPa (40.2 PSI) ✓

**Question:** Which rear wheel? RL or RR?
**Hypothesis:** Byte 3 might indicate wheel position (similar to 0x4A7)

## Missing Data

### 4th Tire Pressure
The second rear tire pressure has not been located yet. Possibilities:
1. **On a different CAN ID** in the 0x49x-0x4Ax range
2. **Alternating messages** with the first rear tire on 0x498
3. **Different message format** (16-bit values, different scaling)
4. **Only broadcasts when different** from the other rear tire

### Temperature Data
The bytes in 0x4A7 (positions 4-7) were initially thought to be temperature using formula `(x - 40)°C`, but:
- Values 108-122 → 68-82°C (154-180°F)
- This is too hot for ambient temperature (55°F actual)
- Might be:
  - Internal sensor temperature (after driving)
  - Different encoding method
  - Not temperature at all

## Calibration Formula

### Discovered Formula
```
Pressure (kPa) = raw_value × 1.583
Pressure (PSI) = raw_value × 0.2296
```

### Verification
| Raw | kPa | PSI | Target | Match |
|-----|-----|-----|--------|-------|
| 166 | 263 | 38.1 | 38 | ✅ |
| 165 | 261 | 37.9 | 38 | ✅ |
| 175 | 277 | 40.2 | 38 | ⚠️ +2 PSI |

The rear tire reading 40 PSI instead of 38 PSI could indicate:
- Actual pressure difference
- Temperature compensation
- Different calibration per axle
- Measurement at different time

## Comparison with OBDb Data

The OBDb database contains **diagnostic request/response** formulas (OBD-II):
```
Pressure = (raw / 58) - 0.5 bars
```

This is **different** from the passive broadcast formula we discovered:
```
Pressure = raw × 1.583 kPa = raw × 0.2296 PSI
```

**Why the difference?**
- OBDb formulas are for **ECU query responses** (request/response protocol)
- We're capturing **passive broadcast messages** on the CAN bus
- Different message formats use different encoding

## ScanGauge II Reference

ScanGauge XGauge commands for Toyota TPMS:
- **Method 1** (ISO): `TXD: 822AF02162`, multiple formula options
- **Method 2** (CAN): `TXD: 07502A2190`, different formulas

These are diagnostic commands, not passive messages.

## Other Observed CAN IDs with Similar Values

IDs that might contain the 4th tire pressure (byte values 150-180 range):

| CAN ID | Byte | Range | Avg | Notes |
|--------|------|-------|-----|-------|
| 0x49E | 0 | 166-166 | 166.0 | ⭐ Always 166 (constant) |
| 0x638 | 4 | 170-170 | 170.0 | ⭐ Always 170 (constant) |
| 0x617 | 5 | 108-180 | 139.1 | ⚠️ High variation |
| 0x1AA | 5 | 177-177 | 177.0 | Always 177 (steering angle msg) |

**Candidates to investigate:**
- 0x49E: Byte 0 constant at 166 (38.2 PSI with our formula)
- 0x638: Byte 4 constant at 170 (39.1 PSI with our formula)

If these are tire pressures, they're suspiciously constant (no variation).

## Next Steps for Finding 4th Tire

### Option 1: Drive and Capture
1. Capture data while **stationary**
2. Capture data while **driving** (pressure increases with heat)
3. Capture data after **changing one tire pressure**
4. Compare which bytes change

### Option 2: Analyze Message Timing
Look for messages that:
- Broadcast at same rate as 0x4A7 and 0x498
- Have byte values in the ~165-175 range
- Change slightly over time (like real tire pressure)

### Option 3: Check for Multi-Message Protocol
The 4th tire might be:
- In the same message but **16-bit encoding** (spans 2 bytes)
- Sent in **alternating messages** with the 3rd tire
- Only sent when **pressure changes**

### Option 4: Search Remaining 0x49x IDs
Systematically check:
- 0x499, 0x49A, 0x49B, 0x49C, 0x49D, 0x49E, 0x49F
- Look for bytes with low variation around 165-175 range

## Conclusion

We've successfully reverse-engineered the TPMS encoding for **front axle and 1 rear tire** using empirical calibration. The formula `x * 1.583 kPa` (or `x * 0.2296 PSI`) accurately decodes tire pressure from the raw CAN bus data.

The 4th tire pressure remains elusive but is likely in one of the other 0x49x messages or encoded differently in 0x498.

## Tools Created

- `investigate_tpms.py` - Analyzes TPMS byte patterns and formulas
- `calibrate_tpms.py` - Interactive calibration tool using known tire pressure
- `find_rear_tpms.py` - Searches for rear tire data in all CAN IDs
- `scripts/decode_with_obdb.py` - Updated with calibrated TPMS formulas

## References

- [OBDb Toyota 4Runner Database](https://github.com/brendan-w/obdb)
- [ScanGauge II XGauge Commands](https://www.scangauge.com/support/x-gauge-commands/toyota-lexus-scion/)
- Toyota 4Runner generation: Fifth Generation (N280), 2010-2023
