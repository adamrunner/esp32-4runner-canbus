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
- ✅ TPMS (Tire Pressure Monitoring System) decoding
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
python analysis/dbc_decode.py logs/CAN_20260104_143052.csv --dbc toyota_4runner.dbc
```

### 4. View Specific Data

```bash
# Check TPMS (tire pressure & temperature)
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv --id 0x4A7

# Check engine RPM
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv --id 0x1D0

# Export all decoded data to CSV
./scripts/decode_with_obdb.py logs/CAN_20260104_143052.csv --export
```

## Analysis Scripts

| Script | Purpose | Best For |
|--------|---------|----------|
| **analysis/bin_to_csv.py** | Binary log converter | Converting SD card logs to CSV |
| **analysis/dbc_decode.py** | DBC signal decoder | Decoding with manufacturer DBC files |
| **scripts/decode_with_obdb.py** ⭐ | OBDb-integrated decoder | Getting accurate physical values (PSI, °C, RPM, etc.) |
| **scripts/validate_can.py** | Message validation | Checking capture quality and statistics |
| **scripts/find_tpms.py** | TPMS message finder | Searching for tire pressure data |
| **scripts/decode_can.py** | Pattern analyzer | Analyzing unknown CAN IDs |

See [SCRIPTS_USAGE.md](SCRIPTS_USAGE.md) and [BINARY_LOGGING.md](BINARY_LOGGING.md) for detailed documentation.

## Currently Decoded Signals

### Broadcast Messages (Real-time on device)

| CAN ID | Description | Signals |
|--------|-------------|---------|
| 0x024 | Kinematics | Yaw rate (deg/s), Steering torque, Lateral G |
| 0x025 | Steering Sensor | Steering angle (degrees) |
| 0x0AA | TPMS Pressure | FL, FR, RL, RR tire pressure (psi) |
| 0x0B4 | Vehicle Speed | Speed (km/h) |
| 0x1C4 | Throttle | Throttle position (%) |
| 0x1D0 | Engine | RPM (rpm) |
| 0x3BB | Engine Coolant (candidate) | ECT (C) from b2 (scaled) |

### OBD/Diagnostic Responses

| CAN ID | Description | Signals |
|--------|-------------|---------|
| 0x7B8 | ABS Module | Lateral G, Yaw rate, Steering angle (PID 0x47) |
| 0x7E8 | ECU | RPM, Speed, Throttle, Temps, Battery voltage |
| OBDb RAX IDs | OBDb derived | Any response IDs defined in `default.json` |

## Example Output

```bash
$ ./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --id 0x0AA

📡 DETAILED DECODE: 0x0AA
============================================================
Total messages: 2689
Name: TPMS Pressure

📝 Sample Decoded Messages:
  FL Pressure: 32.72 psi   FR Pressure: 32.72 psi
  RL Pressure: 32.72 psi   RR Pressure: 32.72 psi

📈 Value Ranges:
  FL Pressure: min=30.94  max=43.85  avg=36.41
  FR Pressure: min=30.40  max=43.96  avg=36.43
```

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
│   ├── dbc_decode.py            # DBC signal decoder
│   └── decoded/                 # Decoded output (gitignored)
├── logs/                        # Captured CAN logs
├── scripts/                     # Python analysis tools
│   ├── decode_with_obdb.py      # ⭐ OBDb-integrated decoder
│   ├── validate_can.py          # CAN validation
│   ├── find_tpms.py             # TPMS finder
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
- Improving TPMS decoding accuracy
- Adding real-time plotting capabilities
- Web interface for live monitoring

## License

See [LICENSE](LICENSE) file for details.

## Acknowledgments

- [OBDb Project](https://github.com/brendan-w/obdb) for Toyota 4Runner signal definitions
- ESP-IDF framework
- Toyota 4Runner community for CAN bus documentation
