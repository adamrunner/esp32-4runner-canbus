# CAN Log Analysis Scripts Usage Guide

Python analysis tools for analyzing and validating CAN bus captures from the 4Runner project.

## Log Formats

The project supports multiple log formats:

| Format | Extension | Description |
|--------|-----------|-------------|
| Binary | `.bin` | Device SD card logs (24-byte records with 64-byte header) |
| CSV | `.csv` | Converted from binary, compatible with all analysis scripts |
| Text | `.log` | Serial capture logs (legacy) |

## Binary Log Tools (analysis/)

Tools in the `analysis/` directory handle binary logs from the device SD card.

### bin_to_csv.py - Binary to CSV Converter

Converts binary CAN logs to CSV format for use with analysis scripts.

**Usage:**
```bash
python analysis/bin_to_csv.py <input.bin> [output.csv]
```

**Examples:**
```bash
# Convert binary log (output name auto-generated)
python analysis/bin_to_csv.py logs/CAN_20260104_143052.bin

# Specify output path
python analysis/bin_to_csv.py logs/CAN_20260104_143052.bin analysis/capture.csv
```

**Output CSV columns:** `datetime,timestamp_us,can_id,dlc,b0,b1,b2,b3,b4,b5,b6,b7`

**Requirements:** Python 3.6+ (standard library only)

---

### dbc_decode.py - DBC Signal Decoder

Decodes CAN messages using DBC database files and outputs decoded signal values.

**Usage:**
```bash
python analysis/dbc_decode.py <log.csv> --dbc <file.dbc> [options]
```

**Options:**
| Option | Description |
|--------|-------------|
| `--dbc PATH` | DBC file to load (can specify multiple times) |
| `--ids ID [ID ...]` | CAN IDs to decode (hex, e.g., `0x024 0x025`) |
| `--out-dir DIR` | Output directory (default: `analysis/decoded`) |
| `--compare-obd` | Compare decoded signals against OBD PID 0x47 responses |

**Examples:**
```bash
# Decode all DBC-defined messages
python analysis/dbc_decode.py logs/capture.csv --dbc toyota_4runner.dbc

# Decode specific CAN IDs only
python analysis/dbc_decode.py logs/capture.csv --dbc toyota.dbc --ids 0x024 0x025

# Compare broadcast vs diagnostic data
python analysis/dbc_decode.py logs/capture.csv --dbc toyota.dbc --compare-obd
```

**Output:** Creates one CSV per message ID in `analysis/decoded/`:
```
analysis/decoded/capture_024_KINEMATICS.csv
analysis/decoded/capture_025_STEERING_SENSOR.csv
```

**Requirements:** Python 3.6+, pandas, numpy, cantools
```bash
pip install pandas numpy cantools
```

See [BINARY_LOGGING.md](BINARY_LOGGING.md) for binary format specification and detailed usage.

---

## Analysis Scripts (scripts/)

Scripts in the `scripts/` directory analyze CSV and text log files.

### 1. decode_with_obdb.py - Enhanced OBDb Decoder ⭐ RECOMMENDED
Advanced decoder that integrates Toyota 4Runner OBDb database for accurate signal decoding.

**Usage:**
```bash
./scripts/decode_with_obdb.py <log_file> [--id 0xXXX] [--export] [--obdb <path>]
```

**Examples:**
```bash
# Show summary of all decodable messages
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log

# Decode specific CAN ID with calibrated values
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --id 0x4A7

# Export all decoded data to CSV
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --export
```

**What it does:**
- Loads signal definitions from Toyota 4Runner OBDb database
- Decodes messages with proper scaling, offsets, and units
- Currently decodes:
  - 0x0AA - TPMS Pressure (psi, calibrated 16-bit pairs)
  - 0x0B4 - Vehicle Speed (km/h)
  - 0x1AA - Steering Angle (degrees)
  - 0x1C4 - Throttle Position (%)
  - 0x1D0 - Engine RPM (rpm)
  - 0x2C1 - Engine Coolant Temperature (°C)
  - 0x4A7 - TPMS Temperature (°C, OBDb format)
  - OBDb `rax` IDs - any response IDs present in `default.json`
- Shows value ranges (min/max/avg) for signals
- Exports decoded data to CSV with proper units

**When to use:**
- **Use this first!** It provides the most accurate decoding
- When you want actual physical values (PSI, °C, RPM, etc.)
- To verify sensor readings are within expected ranges
- For data analysis and visualization

---

### 2. validate_can.py - CAN Message Validator
Validates CAN messages and provides comprehensive statistics about your captures.

**Usage:**
```bash
./scripts/validate_can.py <log_file>
```

**Example:**
```bash
./scripts/validate_can.py logs/can_capture_20251212_164502.log
```

**What it does:**
- Validates all CAN messages in the log
- Counts total messages and unique CAN IDs
- Shows DLC (Data Length Code) distribution
- Lists the top 10 most frequent CAN IDs
- Displays sample messages
- Reports any parsing errors

**When to use:**
- After capturing new logs to ensure data quality
- To quickly check if you're getting valid CAN traffic
- To get an overview of what's in a capture file

---

### 2. find_tpms.py - TPMS Message Finder
Searches for and decodes Tire Pressure Monitoring System (TPMS) messages.

**Usage:**
```bash
./scripts/find_tpms.py <log_file> [--export]
```

**Examples:**
```bash
# Search for TPMS messages
./scripts/find_tpms.py logs/can_capture_20251212_164502.log

# Search and export to file
./scripts/find_tpms.py logs/can_capture_20251212_164502.log --export
```

**What it does:**
- Scans for known TPMS CAN IDs (0x4A7, 0x4A8, 0x4A9, 0x4AA, 0x750)
- Searches for potential TPMS IDs in typical ranges
- Decodes 0x4A7 messages (common Toyota TPMS format)
- Shows data patterns and variations
- Optionally exports TPMS messages to a separate file

**When to use:**
- When looking for tire pressure data
- To verify TPMS sensors are broadcasting
- To analyze TPMS data patterns

**Note:** The decoder provides multiple interpretations (8-bit and 16-bit) since the exact format needs calibration against known tire pressures.

---

### 3. decode_can.py - CAN Data Decoder
Decodes and analyzes specific CAN IDs with detailed pattern analysis.

**Usage:**
```bash
./scripts/decode_can.py <log_file> [--id 0xXXX] [--export]
```

**Examples:**
```bash
# Show summary of all CAN IDs
./scripts/decode_can.py logs/can_capture_20251212_164502.log

# Analyze a specific CAN ID in detail
./scripts/decode_can.py logs/can_capture_20251212_164502.log --id 0x0AA

# Export all decoded data to CSV
./scripts/decode_can.py logs/can_capture_20251212_164502.log --export
```

**What it does:**
- Lists all CAN IDs found with message counts
- Shows detailed analysis for specific IDs:
  - Most common data patterns
  - Byte-by-byte variation analysis
  - Temporal changes (first, middle, last messages)
- Attempts to decode known IDs (wheel speed, vehicle speed, steering angle, RPM)
- Can export all data to CSV format

**When to use:**
- To understand what data a specific CAN ID contains
- To find patterns in CAN messages
- To identify which bytes change and which are constant
- To verify data is being captured correctly

**Known Decodable IDs:**
- 0x0AA - Wheel Speed (tentative)
- 0x0B4 - Vehicle Speed (tentative)
- 0x1AA - Steering Angle (tentative)
- 0x1D0 - Engine RPM (tentative)

*Note: Decoders are tentative and may need calibration*

---

## Typical Workflow

### 1. Quick Analysis with OBDb Decoder (Recommended)
```bash
# Get decoded summary with actual physical values
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log

# Check TPMS data with calibrated pressure (bars) and temperature (°C)
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --id 0x4A7

# Export decoded data with units for analysis
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --export
```

### 2. Validate Capture Quality
```bash
# Ensure you got valid CAN data
./scripts/validate_can.py logs/can_capture_20251212_164502.log
```

### 3. Search for Specific Messages
```bash
# Search for TPMS messages across all known IDs
./scripts/find_tpms.py logs/can_capture_20251212_164502.log

# Analyze unknown CAN IDs
./scripts/decode_can.py logs/can_capture_20251212_164502.log --id 0x123
```

### 4. Compare Captures
```bash
# Compare TPMS readings between different captures
./scripts/decode_with_obdb.py logs/capture1.log --id 0x4A7 > capture1_tpms.txt
./scripts/decode_with_obdb.py logs/capture2.log --id 0x4A7 > capture2_tpms.txt
diff capture1_tpms.txt capture2_tpms.txt
```

---

## Working with Raw vs Cleaned Logs

These scripts work with **both raw and cleaned log files**:

- **Raw logs** (e.g., `can_capture_20251212_164502.log`): Contains ESP-IDF boot messages, ANSI color codes, and all console output
- **Cleaned logs** (e.g., `can_capture_20251212_164502_clean.log`): Contains only CAN messages, created by running `sanitize_logs.sh`

The scripts automatically handle both formats, so you don't need to clean the logs first unless you want to.

---

## Tips

1. **Start with validation**: Always run `scripts/validate_can.py` first to ensure you have valid data
2. **Use cleaned logs for faster processing**: If you're running scripts multiple times, clean the log first with `sanitize_logs.sh`
3. **Compare captures**: Run the same analysis on different captures to see what changes (e.g., engine on vs off, driving vs parked)
4. **Look for patterns**: Use the byte variation analysis to identify which bytes change with specific vehicle states
5. **Cross-reference**: If you find an interesting pattern, search for that CAN ID online with your vehicle model

---

## Output Files

Scripts can generate the following output files:

- `<logfile>_tpms.txt` - TPMS messages export (from `scripts/find_tpms.py --export`)
- `<logfile>_decoded.csv` - All decoded messages in CSV format (from `scripts/decode_can.py --export`)

---

## Requirements

- Python 3.6 or later
- No external dependencies (uses only Python standard library)

---

## Troubleshooting

**Problem:** "No valid CAN messages found"
- Check if the log file contains CAN data
- Try with a cleaned log file
- Verify the log format matches the expected pattern

**Problem:** Script shows "File not found"
- Ensure you're running from the project root directory
- Use the correct relative path to the log file

**Problem:** Decoders show unexpected values
- Remember that decoders are tentative and need calibration
- Try correlating with known vehicle states (e.g., wheels stationary = low wheel speed values)
- The raw hex values are always accurate; the decoded interpretations may need adjustment
