# CAN Bus Analysis Tools

Tools for analyzing Toyota 4Runner CAN bus logs to identify and decode broadcast messages.

## Overview

This directory contains Python scripts to analyze CAN log files and help identify new broadcast messages on the Toyota 4Runner CAN bus.

## Tools

### 1. can_analyzer.py

General-purpose CAN log analysis tool.

**Features:**
- Message frequency analysis
- Data pattern analysis for specific CAN IDs
- High-frequency message detection
- Temporal pattern analysis
- Constant vs changing message detection
- Export message summaries

**Usage:**
```bash
python3 can_analyzer.py <log_file.csv> [options]

Options:
  --freq N         Show top N most frequent messages (default: 50)
  --analyze ID     Analyze data patterns for specific CAN ID
  --high-freq N    Find messages with >= N msgs/sec (default: 10)
  --temporal ID    Analyze temporal patterns for specific CAN ID
  --constant       Find messages with constant data
  --changing N     Find messages with >= N unique data combos (default: 5)
  --summary FILE   Export message summary to FILE
```

**Examples:**
```bash
# Show top 30 most frequent CAN IDs
python3 can_analyzer.py logs/CAN_0001.CSV --freq 30

# Analyze specific CAN ID 0x024
python3 can_analyzer.py logs/CAN_0001.CSV --analyze 024

# Find high-frequency messages and export summary
python3 can_analyzer.py logs/CAN_0001.CSV --high-freq 10 --summary analysis/summary.csv
```

---

### 2. wheel_speed_analyzer.py

Specialized tool for validating wheel speed broadcast messages (0x0AA).

**Features:**
- Validates wheel speed decoding
- Statistics for all four wheels
- Driving segment identification (stopped vs moving)
- Turning event detection
- Wheel speed data export

**Usage:**
```bash
python3 wheel_speed_analyzer.py <log_file.csv> [options]

Options:
  --stats           Show wheel speed statistics
  --segments        Identify driving segments
  --turning         Find turning events
  --validate        Validate broadcast decoding
  --export FILE     Export parsed data to FILE
```

**Examples:**
```bash
# Validate wheel speed decoding and show statistics
python3 wheel_speed_analyzer.py logs/CAN_0001.CSV --validate --stats

# Identify driving segments and turning events
python3 wheel_speed_analyzer.py logs/CAN_0001.CSV --segments --turning

# Export parsed wheel speed data
python3 wheel_speed_analyzer.py logs/CAN_0001.CSV --export analysis/wheel_speeds.csv
```

**Wheel Speed Decoding:**
```
CAN ID: 0x0AA
Format: 4x 16-bit big-endian values (one per wheel)
- Bytes 0-1: Front Right
- Bytes 2-3: Front Left
- Bytes 4-5: Rear Right
- Bytes 6-7: Rear Left

Formula: km/h = (raw_16bit - 6750) / 100.0
```

---

### 3. simple_analyzer.py

Quick overview tool for identifying interesting broadcast messages.

**Features:**
- High-frequency message detection (>10 Hz)
- High-variation message detection (>100 unique data combos)
- Sample data display
- Change rate calculation

**Usage:**
```bash
python3 simple_analyzer.py <log_file.csv>
```

**Example:**
```bash
python3 simple_analyzer.py logs/CAN_0001.CSV
```

Output shows:
- Messages with >10 Hz update rate
- Messages with >100 unique data combinations
- Sample hex data for each message

---

### 4. turning_test_analyzer.py

Turning-focused analyzer to identify steering/yaw candidates.

**Features:**
- Uses 0x0AA wheel speed differences to detect turning
- Correlates candidate bytes with left-right wheel speed delta
- Compares turning vs straight deltas for candidate bytes

**Usage:**
```bash
python3 turning_test_analyzer.py <log_file.csv> [options]

Options:
  --candidates IDS   Comma-separated CAN IDs (default: 0B4,2C1,1D0,1C4,024,025)
  --min-speed KPH    Minimum speed to consider (default: 5.0)
  --min-diff KPH     Turning threshold (default: 1.5)
  --straight-max-diff KPH  Straight threshold (default: 0.5)
  --tolerance-ms MS  Timestamp alignment tolerance (default: 20)
  --top N            Top results per metric (default: 5)
```

**Example:**
```bash
python3 turning_test_analyzer.py logs/CAN_20260102_125202.CSV --top 5
```

Use logs with many low-speed turns (parking lots, neighborhoods) for best results.

---

## Log File Format

CSV files should have the following columns:
```
timestamp_us,can_id,dlc,b0,b1,b2,b3,b4,b5,b6,b7
```

Example:
```
54972340,0AA,8,1F,35,1F,48,1F,3C,1F,41
54976954,38F,8,01,00,00,00,00,00,00
```

---

## Key Findings

### Validated Decodings

**Wheel Speed Broadcast (0x0AA)** ✓
- Fully validated
- Update rate: ~12ms (83 Hz)
- All 4 wheels decoded individually
- Consistent with diagnostic wheel speed data

### Top Candidates for Further Investigation

Based on frequency and data variation:

1. **0x024** (46-65 Hz, 63.6% variation)
   - Likely: Engine RPM (high frequency matches engine sensors)
   - Try: 16-bit encodings with division factors (32, 64)

2. **0x2C1** (18-25 Hz, 86.8% variation)
   - Highly dynamic multi-byte data
   - Likely: Transmission or detailed engine data

3. **0x0B4** (23-33 Hz, 88.6% variation)
   - Very dynamic
   - Likely: Vehicle speed proxy (byte 5 ~ speed)

4. **0x1D0** (18-25 Hz, 44.8-59.9% variation)
   - Moderate variation
   - Likely: Gear position or vehicle status

5. **0x1C4** (24-33 Hz, 40.1-52.1% variation)
   - Varies between different vehicle states
   - Likely: Drive mode or status flags

6. **0x025** (46-65 Hz, 9.5% variation)
   - Low variation suggests status/control bits
   - Bytes 4-6 show limited values (0x78, 0x82, 0x64, 0x5A, 0x46, 0x3C)
   - Likely: Gear selector or drive mode

See `CAN_ANALYSIS_REPORT.md` for complete analysis and decoding strategy.

---

## Decoding Strategy

### Phase 1: Correlation Analysis
1. Correlate unknown messages with wheel speed
2. Analyze temporal patterns (stops, starts, gear shifts)
3. Cross-reference with diagnostic PIDs (RPM, gear, throttle, brake)

### Phase 2: Hypothesis Testing
For each candidate CAN ID:
1. Test specific encoding hypotheses (8-bit, 16-bit, big/little endian)
2. Try common scalars and offsets
3. Validate against known vehicle behavior

### Phase 3: Validation
1. Add real-time decoding to ESP32 display
2. Observe values while driving
3. Confirm correlation with expected behavior

---

## Requirements

- Python 3.7+
- pandas
- numpy

Install dependencies:
```bash
pip3 install pandas numpy
```

---

## Contributing

When you successfully decode a new broadcast message:

1. Document the decoding in this README
2. Update `main/4runner_canbus_main.cpp` to decode the message
3. Add validation code
4. Update `CAN_ANALYSIS_REPORT.md` with findings

---

## License

Same as parent esp32-4runner-canbus project.
