# Temperature Sensors Implementation Plan

## Overview
Add additional temperature sensors to the diagnostic and 4Runner pages:
- Engine Coolant Temperature (ECT) - OBD-II query and broadcast candidate
- Engine Oil Temperature (EOT)
- Ambient Air Temperature (AAT)

## Prerequisites
Before starting this work, the current working directory changes need to be committed:
- Screenshot feature (screenshot.cpp, screenshot.h)
- Status message system (app_state.cpp/h)
- Error label additions to logging_page and rpm_page
- Page utils updates for screenshot button

## Temperature Sources

### Standard OBD-II PIDs (Service 0x01)
| PID | Signal | Description | Decode Formula |
|-----|--------|-------------|----------------|
| 0x05 | ECT | Engine Coolant Temperature | `byte[3] - 40` °C |
| 0x46 | AAT | Ambient Air Temperature | `byte[3] - 40` °C |

### Toyota Extended PIDs (Service 0x21)
| Header | PID | Signal | Description | Decode Formula | Byte Position |
|--------|-----|--------|-------------|----------------|---------------|
| 0x7E0 | 0x51 | EOT | Engine Oil Temperature | `byte[9] - 40` °C | bix: 72, len: 8 |
| 0x7E0 | 0x51 | VVTOT | VVT Oil Temperature | `byte[7] - 40` °C | bix: 56, len: 8 |

### Broadcast Message (Passive, No Query Required)
| CAN ID | Signal | Description | Decode Formula |
|--------|--------|-------------|----------------|
| 0x3BB | ECT_BCAST_CAND | Engine Coolant Temperature candidate | `byte[2] * 0.51 - 1.7` °C |

## Implementation Steps

### Phase 1: Add Fields to app_state.h
Add to `can_metrics_t` struct:
```cpp
// Engine temperatures
float ect_c;           // Engine coolant temp from OBD-II PID 0x05 (Celsius)
float ect_broadcast_c; // Engine coolant temp from broadcast 0x2C1 (Celsius)
float ect_bcast_candidate_c; // ECT candidate from broadcast 0x3BB (Celsius)
float eot_c;           // Engine oil temp from Toyota PID 0x51 (Celsius)
float aat_c;           // Ambient air temp from OBD-II PID 0x46 (Celsius)

// Validity flags
bool ect_valid;
bool ect_broadcast_valid;
bool ect_bcast_candidate_valid;
bool eot_valid;
bool aat_valid;
```

### Phase 2: Add OBD-II Query Requests
In `4runner_canbus_main.cpp`, add to the query schedule:

1. Add PID 0x05 (ECT) to existing OBD-II service 0x01 queries
2. Add PID 0x46 (AAT) to existing OBD-II service 0x01 queries
3. Add Toyota PID 0x51 to service 0x21 queries (for EOT)

Query structure for OBD-II PIDs (header 0x7DF or 0x7E0):
- ECT: `02 01 05 00 00 00 00 00` (Service 0x01, PID 0x05)
- AAT: `02 01 46 00 00 00 00 00` (Service 0x01, PID 0x46)

Query structure for Toyota Extended PID (header 0x7E0):
- EOT: `02 21 51 00 00 00 00 00` (Service 0x21, PID 0x51)

### Phase 3: Add Response Handlers
In `4runner_canbus_main.cpp`, handle responses:

1. **OBD-II PID 0x05 (ECT)** - Response on 0x7E8:
   ```cpp
   case 0x05: {
       // Response: 41 05 XX
       float ect = msg->data[3] - 40.0f;
       m->ect_c = ect;
       m->ect_valid = true;
       break;
   }
   ```

2. **OBD-II PID 0x46 (AAT)** - Response on 0x7E8:
   ```cpp
   case 0x46: {
       // Response: 41 46 XX
       float aat = msg->data[3] - 40.0f;
       m->aat_c = aat;
       m->aat_valid = true;
       break;
   }
   ```

3. **Toyota PID 0x51 (EOT)** - Response on 0x7E8:
   ```cpp
   case 0x51: {
       // Multi-frame response, EOT at byte offset 9 (bix 72)
       // VVTOT at byte offset 7 (bix 56) - optional
       float eot = response_data[9] - 40.0f;
       m->eot_c = eot;
       m->eot_valid = true;
       break;
   }
   ```

4. **Broadcast 0x3BB (ECT candidate)** - Passive listener:
   ```cpp
   case 0x3BB: {
       float ect = (msg->data[2] * 0.51f) - 1.7f;
       m->ect_bcast_candidate_c = ect;
       m->ect_bcast_candidate_valid = true;
       break;
   }
   ```

### Phase 4: Update Diagnostic Page (diag_page.cpp)
Add metric cards for:
- ECT (OBD-II) - "ECT (C)"
- ECT (3BB) - "ECT 3BB (C)"
- AAT - "Ambient (C)"

Update `on_update` callback to display values.

### Phase 5: Update 4Runner Page (fourrunner_page.cpp)
Add metric cards for:
- ECT (Broadcast 0x2C1, legacy) - "ECT Bcast (C)"
- EOT - "Oil Temp (C)"

Update `on_update` callback to display values.

### Phase 6: Testing
1. Build and flash: `idf.py build flash monitor`
2. Verify ECT and ECT 3BB appear on diagnostic page
3. Verify ECT broadcast (legacy) and EOT appear on 4Runner page
4. Compare ECT values between OBD-II query and 0x3BB candidate (should match)
5. Verify AAT shows reasonable ambient temperature

## File Changes Summary
| File | Changes |
|------|---------|
| main/app_state.h | Add temperature fields and validity flags |
| main/4runner_canbus_main.cpp | Add queries and response handlers |
| main/pages/diag_page.cpp | Add ECT, ECT 3BB, and AAT display |
| main/pages/fourrunner_page.cpp | Add ECT broadcast (legacy) and EOT display |

## Notes
- The ECT candidate (0x3BB byte 2) is passive and doesn't require sending queries
- EOT uses Toyota's extended diagnostic service (0x21) which may require multi-frame handling
- OBD-II temps use the standard formula: `raw_value - 40` for Celsius
- Consider adding a temperature comparison view to validate ECT sources match
- 2026-01-25 idle log confirmed 0x3BB b2 tracks OBD-II ECT closely and provides finer resolution.
- 0x2C1 b0 stayed at `0x08` (=-32C with `raw - 40`) and is likely not ECT.
