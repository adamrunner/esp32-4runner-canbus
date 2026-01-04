# Binary CAN Logging

This document describes the binary CAN log format used by the ESP32 CAN logger and the Python tools for converting and decoding binary logs.

## Overview

Binary logging provides a compact, efficient format for capturing CAN bus traffic. Compared to CSV logging:

- **~3x smaller files**: 24 bytes per message vs ~60-80 bytes in CSV
- **Lower CPU overhead**: No string formatting during capture
- **Faster SD writes**: Fixed-size records, sequential writes
- **Self-describing**: Versioned header with metadata for future compatibility

## Binary Format Specification (v1)

### File Structure

```
┌──────────────────────────────────────┐
│           Header (64 bytes)          │
├──────────────────────────────────────┤
│         Record 0 (24 bytes)          │
├──────────────────────────────────────┤
│         Record 1 (24 bytes)          │
├──────────────────────────────────────┤
│              ...                     │
├──────────────────────────────────────┤
│         Record N (24 bytes)          │
└──────────────────────────────────────┘
```

### Header Layout (64 bytes, little-endian)

| Offset | Size | Type     | Field                   | Description                              |
|--------|------|----------|-------------------------|------------------------------------------|
| 0      | 8    | char[8]  | magic                   | `"CANBIN\0\0"` - file identifier         |
| 8      | 2    | uint16   | version                 | Format version (currently 1)             |
| 10     | 2    | uint16   | header_size             | Header size in bytes (64)                |
| 12     | 8    | uint64   | log_start_unix_us       | Unix timestamp (microseconds) at log start, 0 if RTC invalid |
| 20     | 8    | uint64   | log_start_monotonic_us  | ESP32 monotonic timestamp at log start   |
| 28     | 4    | uint32   | record_size             | Record size in bytes (24)                |
| 32     | 4    | uint32   | flags                   | Reserved flags (0)                       |
| 36     | 28   | uint8[]  | reserved                | Reserved for future use                  |

### Record Layout (24 bytes, little-endian)

| Offset | Size | Type     | Field        | Description                          |
|--------|------|----------|--------------|--------------------------------------|
| 0      | 8    | uint64   | timestamp_us | Monotonic timestamp (microseconds)   |
| 8      | 4    | uint32   | can_id       | CAN arbitration ID                   |
| 12     | 1    | uint8    | dlc          | Data Length Code (0-8)               |
| 13     | 1    | uint8    | flags        | Reserved (0)                         |
| 14     | 8    | uint8[8] | data         | CAN payload (padded with zeros)      |
| 22     | 2    | uint16   | reserved     | Reserved for alignment               |

### Timestamp Reconstruction

Records store monotonic timestamps from `esp_timer_get_time()`. To reconstruct wall-clock time:

```
if log_start_unix_us != 0:
    unix_us = log_start_unix_us + (record.timestamp_us - log_start_monotonic_us)
else:
    # RTC was not valid at log start; no wall-clock available
```

### File Naming

Binary log files use the extension `.bin` and follow the pattern:
```
CAN_YYYYMMDD_HHMMSS.bin
```

Example: `CAN_20260104_143052.bin`

## Analysis Tools

The `analysis/` directory contains Python tools for working with binary logs.

### bin_to_csv.py - Binary to CSV Converter

Converts binary CAN logs to CSV format compatible with existing analysis scripts.

**Usage:**
```bash
python analysis/bin_to_csv.py <input.bin> [output.csv]
```

**Examples:**
```bash
# Convert to CSV (auto-generates output name)
python analysis/bin_to_csv.py logs/CAN_20260104_143052.bin
# Output: logs/CAN_20260104_143052.csv

# Specify output path
python analysis/bin_to_csv.py logs/CAN_20260104_143052.bin analysis/decoded/capture.csv
```

**Output CSV Format:**
```csv
datetime,timestamp_us,can_id,dlc,b0,b1,b2,b3,b4,b5,b6,b7
2026-01-04 14:30:52,1234567890,0B4,8,00,00,12,34,00,00,00,00
```

- `datetime`: Wall-clock time (empty if RTC was invalid)
- `timestamp_us`: Monotonic timestamp in microseconds
- `can_id`: CAN ID in uppercase hex (e.g., `0B4`)
- `dlc`: Data Length Code
- `b0`-`b7`: Payload bytes in uppercase hex

**Requirements:** Python 3.6+ (standard library only)

---

### dbc_decode.py - DBC Signal Decoder

Decodes CAN messages using DBC database files and outputs signal values to CSV.

**Usage:**
```bash
python analysis/dbc_decode.py <log.csv> --dbc <file.dbc> [--dbc <another.dbc>] [options]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--dbc PATH` | DBC file to load (can specify multiple) |
| `--ids ID [ID ...]` | CAN IDs to decode (hex, e.g., `0x024 0x025`) |
| `--out-dir DIR` | Output directory (default: `analysis/decoded`) |
| `--compare-obd` | Compare decoded signals against OBD PID 0x47 responses |

**Examples:**
```bash
# Decode all messages defined in DBC
python analysis/dbc_decode.py logs/capture.csv --dbc toyota_4runner.dbc

# Decode specific CAN IDs
python analysis/dbc_decode.py logs/capture.csv --dbc toyota.dbc --ids 0x024 0x025

# Use multiple DBC files
python analysis/dbc_decode.py logs/capture.csv --dbc chassis.dbc --dbc powertrain.dbc

# Compare broadcast kinematics against diagnostic responses
python analysis/dbc_decode.py logs/capture.csv --dbc toyota.dbc --compare-obd
```

**Output:**
Creates one CSV per decoded message ID in the output directory:
```
analysis/decoded/
├── capture_024_KINEMATICS.csv
├── capture_025_STEERING_SENSOR.csv
└── capture_0B4_VEHICLE_SPEED.csv
```

Each CSV contains `timestamp_us` plus all decoded signal columns with physical values.

**Requirements:** Python 3.6+, pandas, numpy, cantools

```bash
pip install pandas numpy cantools
```

---

## Typical Workflow

### 1. Capture Binary Logs

Binary logging is enabled by default on the device. Logs are saved to the SD card as `.bin` files.

### 2. Convert to CSV

```bash
# Copy log from SD card, then convert
python analysis/bin_to_csv.py /path/to/CAN_20260104_143052.bin
```

### 3. Analyze with Existing Tools

The converted CSV works with all existing analysis scripts:

```bash
# Use OBDb decoder
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv

# Validate capture
./scripts/validate_can.py logs/CAN_20260104_143052.csv
```

### 4. Decode with DBC (Optional)

For signals defined in DBC files (e.g., from OBDb or manufacturer sources):

```bash
python analysis/dbc_decode.py logs/CAN_20260104_143052.csv \
    --dbc ~/Code/OBDb-Toyota-4Runner/dbc/toyota_4runner.dbc
```

---

## Decoded Broadcast Signals

The firmware now decodes kinematics data from broadcast CAN messages in real-time, displayed on the Orientation page alongside diagnostic (OBD) data for comparison.

### 0x024 - Kinematics

| Signal | Bits | Offset | Scale | Unit |
|--------|------|--------|-------|------|
| YAW_RATE | 1-10 (10-bit) | -512 | 1.0 | deg/s |
| STEER_TORQUE | 17-26 (10-bit) | -512 | 1.0 | - |
| ACCEL_Y | 33-42 (10-bit) | -512 | -0.002121 | g |

### 0x025 - Steering Angle Sensor

| Signal | Bits | Offset | Scale | Unit |
|--------|------|--------|-------|------|
| STEER_ANGLE | 3-14 (12-bit signed) | 0 | 1.5 | deg |

These signals are extracted using big-endian bit indexing (LSB start) as defined in Toyota DBC files.

---

## Verifying Log Integrity

To verify a binary log file:

```bash
# Check file size matches expected record count
python -c "
import os
header_size = 64
record_size = 24
file_size = os.path.getsize('logs/CAN_20260104_143052.bin')
records = (file_size - header_size) // record_size
print(f'File size: {file_size} bytes')
print(f'Expected records: {records}')
print(f'Leftover bytes: {(file_size - header_size) % record_size}')
"
```

A valid file should have 0 leftover bytes. Non-zero indicates truncation (e.g., power loss during logging).

---

## Troubleshooting

**Problem:** `bin_to_csv.py` reports "Bad magic"
- The file is not a valid binary CAN log or is corrupted
- Check that you're using a `.bin` file from the CAN logger, not a different binary format

**Problem:** `dbc_decode.py` shows decode errors
- DBC signal definitions may not match the actual message format
- Try `--compare-obd` to validate against known OBD responses
- Check DBC bit ordering (big-endian vs little-endian start bit)

**Problem:** Empty datetime column in CSV
- The RTC was not valid when logging started
- Timestamps are still valid (monotonic), just without wall-clock reference

**Problem:** Truncated file warning
- Logging was interrupted (e.g., power loss, SD card removal)
- The file is still usable; only the incomplete final record is skipped
