# ESP32 4Runner CAN Bus Project

ESP32-S3 based CAN bus listener for Toyota 4Runner, capturing and decoding vehicle data in real-time.

## Project Overview

This project uses an ESP32-S3 microcontroller to passively listen to the CAN bus of a Toyota 4Runner, capturing messages for analysis. The captured data can be decoded using the included Python scripts that leverage the [OBDb Toyota 4Runner database](https://github.com/brendan-w/obdb) for accurate signal interpretation.

## Hardware

- **ESP32-S3** microcontroller
- **CAN transceiver** (SN65HVD230 or similar)
- **CAN bus connection** to 4Runner OBD-II port

## Features

- ✅ Passive CAN bus monitoring (listen-only mode)
- ✅ Real-time message capture to serial output
- ✅ Binary CAN logging to SD card (compact, efficient)
- ✅ Automatic log capture scripts
- ✅ Comprehensive Python analysis tools
- ✅ OBDb-integrated decoder for accurate signal interpretation
- ✅ DBC-based signal decoding with cantools
- ❌ TPMS: not on this truck's OBD-port CAN bus; see [TPMS_DECODING_NOTES.md](TPMS_DECODING_NOTES.md)
- ✅ Real-time kinematics decoding (yaw rate, lateral G, steering angle)
- ✅ Vehicle speed, RPM, throttle, and more

## Quick Start

### 1. Capture CAN Data

**On-device logging (SD card):**
The device logs CAN traffic directly to SD card in binary format (`.bin` files). Binary logging is compact and efficient, using only 24 bytes per message.

**Sync SD card logs:**
Copy `CAN_*.bin` (and any `CAN_*.csv` if present) from the SD card into `logs/`.
If you use the Codex sync helper, it supports both `.bin` and `.csv` by default; override `--pattern` if needed.

**Serial capture:**
```bash
# Start capturing CAN bus traffic via serial
./capture_can.sh
```

Press `Ctrl+]` to stop capturing.

### 2. Convert Binary Logs

Binary logs from the SD card need to be converted to CSV for analysis:

```bash
# Convert binary log to CSV
python analysis/bin_to_csv.py logs/CAN_20260104_143052.bin
# Output: logs/CAN_20260104_143052.csv
```

See [BINARY_LOGGING.md](BINARY_LOGGING.md) for format details and advanced usage.

### 3. Analyze the Data

```bash
# Recommended: Use the OBDb decoder for accurate physical values
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv

# Or decode using DBC files
python analysis/dbc_decode.py logs/CAN_20260104_143052.csv --dbc dbc/toyota_4runner_2018.dbc
```

### 4. View Specific Data

```bash
# Check wheel speeds
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv --id 0x0AA

# Check engine RPM (0x1C4, from the project DBC)
python analysis/dbc_decode.py logs/CAN_20260104_143052.csv --dbc dbc/toyota_4runner_2018.dbc --ids 0x1C4

# Export all decoded data to CSV
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv --export
```

## Analysis Scripts

| Script | Purpose | Best For |
|--------|---------|----------|
| **analysis/bin_to_csv.py** | Binary log converter | Converting SD card logs to CSV |
| **analysis/bin_to_candump.py** | candump log converter | Opening logs in CANExplorer, SavvyCAN, can-utils |
| **analysis/dbc_decode.py** | DBC signal decoder | Decoding with `dbc/toyota_4runner_2018.dbc` |
| **analysis/cx_fields.py** | CANExplorer field analysis | Checksums, counters, fields per ID, per log |
| **analysis/cx_consensus.py** | Cross-log field summary | Fields seen in most logs, matched to reference DBCs |
| **analysis/cx_obdref.py** | OBD reference matching | Finding the broadcast that carries a polled PID |
| **analysis/cx_markers.py** | Event marker search | Finding bits that change at moments you noted |
| **scripts/decode_with_obdb.py** ⭐ | OBDb-integrated decoder | Getting accurate physical values (PSI, °C, RPM, etc.) |
| **scripts/validate_can.py** | Message validation | Checking capture quality and statistics |
| **scripts/find_tpms.py** | TPMS message finder (legacy) | Superseded by tpms_correlate.py |
| **scripts/tpms_rf_record.py** | RF receiver recorder | Logging `tpms.local` pressures as ground truth |
| **scripts/tpms_correlate.py** | CAN vs RF correlator | Finding CAN signals that track a measured quantity |
| **scripts/decode_can.py** | Pattern analyzer | Analyzing unknown CAN IDs |

See [SCRIPTS_USAGE.md](SCRIPTS_USAGE.md) and [BINARY_LOGGING.md](BINARY_LOGGING.md) for detailed documentation.

## Currently Decoded Signals

### Broadcast Messages

[`dbc/toyota_4runner_2018.dbc`](../dbc/toyota_4runner_2018.dbc) is the source of truth;
evidence and open candidates are in
[`analysis/QUICK_REFERENCE.md`](../analysis/QUICK_REFERENCE.md).

| CAN ID | Description | Signals | On device |
|--------|-------------|---------|-----------|
| 0x024 | Kinematics | Yaw rate (deg/s), Steering torque, Lateral G | Yes |
| 0x025 | Steering Sensor | Steering angle (degrees) | Yes |
| 0x0AA | Wheel Speed | FR, FL, RR, RL (km/h), (raw - 6767) / 100 | Yes |
| 0x0B4 | Vehicle Speed | Speed (km/h), bytes 5-6 / 100 | Yes |
| 0x1C4 | Engine | RPM, bytes 0-1 * 25/32 | Yes |
| 0x224 | Brake | Brake pressed | No |
| 0x2C1 | Gas Pedal | Gas released, pedal position | No |
| 0x361 | Engine | Throttle position (%) | No |
| 0x3D0 | Vehicle Speed | Speed (km/h), byte 0 | No |
| 0x498 | Engine | Coolant temp, byte 3 * 1.25 (C) | Yes |
| 0x49B | Engine | Intake air temp, byte 7 * 1.25 (C) | No |

### OBD/Diagnostic Responses

| CAN ID | Description | Signals |
|--------|-------------|---------|
| 0x7B8 | ABS Module | Lateral G, Yaw rate, Steering angle (PID 0x47) |
| 0x7C8 | Combination meter | Fuel level (21 29) |
| 0x758 (ext 0xDC) | 0x750 sub-ECU | Supply voltage, 0.1 V/count (21 08; not polled by default) |
| 0x7E8 | ECU | RPM, Speed, Throttle, Temps, Battery voltage |
| OBDb RAX IDs | OBDb derived | Any response IDs defined in `default.json` |

## Example Output

```bash
$ ./scripts/tpms_correlate.py logs/CAN_20261025_121704.bin logs/rf_20261025.jsonl --clock-offset 31204.5

    ID signal     wheel  r_step       r  lag s  bins     raw range counts/PSI
0x3B1 b3         FL      0.584   0.150      4   345      1-5            0.01
0x620 b2         FL     -0.576  -0.137     18   345      0-1           -0.00
...
```

(No signal tracks pressure here: a real one would have `r_step` and `r` both
near ±1. See [TPMS_DECODING_NOTES.md](TPMS_DECODING_NOTES.md).)

## File Structure

```
.
├── docs/                        # Project documentation
│   ├── README.md                # This file
│   ├── BINARY_LOGGING.md        # Binary log format & tools
│   ├── SCRIPTS_USAGE.md         # Detailed script documentation
│   ├── 4RUNNER_CAN_ANALYSIS_SUMMARY.md
│   └── TPMS_DECODING_NOTES.md
├── main/                        # ESP32 firmware
├── components/                  # ESP-IDF components
│   └── can_logger/              # Binary CAN logging component
├── analysis/                    # Binary log analysis tools
│   ├── bin_to_csv.py            # Binary to CSV converter
│   ├── bin_to_candump.py        # Binary/CSV to candump converter
│   ├── dbc_decode.py            # DBC signal decoder
│   ├── cx_*.py                  # CANExplorer-based analysis (fields, consensus, OBD refs, markers)
│   └── decoded/                 # Decoded output (gitignored)
├── dbc/
│   └── toyota_4runner_2018.dbc  # Decoded broadcast signals
├── logs/                        # Captured CAN logs
├── scripts/                     # Python analysis tools
│   ├── decode_with_obdb.py      # ⭐ OBDb-integrated decoder
│   ├── validate_can.py          # CAN validation
│   ├── find_tpms.py             # TPMS finder (legacy)
│   ├── tpms_rf_record.py        # Record RF receiver pressures
│   ├── tpms_correlate.py        # Correlate CAN signals with RF pressures
│   └── decode_can.py            # Pattern analyzer
├── capture_can.sh               # Capture helper
├── sanitize_logs.sh             # Log cleaning
└── README.md                    # Project entrypoint
```

## Development

### Building the Firmware

```bash
idf.py build
idf.py flash monitor
```

### Adding New Signal Decoders

Edit [scripts/decode_with_obdb.py](../scripts/decode_with_obdb.py) and add your signal definition to the `passive_can_ids` dictionary:

```python
0x123: {
    "name": "My Signal",
    "signals": [
        {"name": "Value", "bix": 0, "len": 8, "div": 10, "unit": "rpm"},
    ]
}
```

See the [OBDb format documentation](https://github.com/brendan-w/obdb) for signal format details.

## Integration with OBDb

This project uses signal definitions from the [OBDb Toyota 4Runner repository](https://github.com/brendan-w/obdb). The OBDb data provides calibrated scaling factors, offsets, and units for accurate decoding of vehicle signals.

**Expected OBDb location:** `~/Code/OBDb-Toyota-4Runner/signalsets/v3/default.json`

To use a different location:
```bash
./scripts/decode_with_obdb.py <log_file> --obdb /path/to/default.json
```

## Contributing

Contributions welcome! Areas of interest:
- Adding more passive CAN ID decoders
- Finding where the TPMS ECU lives (K-line on DLC3 pin 7?)
- Adding real-time plotting capabilities
- Web interface for live monitoring

## License

See [LICENSE](LICENSE) file for details.

## Acknowledgments

- [OBDb Project](https://github.com/brendan-w/obdb) for Toyota 4Runner signal definitions
- ESP-IDF framework
- Toyota 4Runner community for CAN bus documentation
