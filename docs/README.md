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
- ✅ Automatic log capture scripts
- ✅ Comprehensive Python analysis tools
- ✅ OBDb-integrated decoder for accurate signal interpretation
- ✅ TPMS (Tire Pressure Monitoring System) decoding
- ✅ Vehicle speed, RPM, throttle, and more

## Quick Start

### 1. Capture CAN Data

```bash
# Start capturing CAN bus traffic
./capture_can.sh
```

Press `Ctrl+]` to stop capturing. The log will be saved to `logs/can_capture_YYYYMMDD_HHMMSS.log`.

### 2. Analyze the Data

```bash
# Recommended: Use the OBDb decoder for accurate physical values
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log

# Or run a complete analysis
./scripts/analyze_log.sh logs/can_capture_20251212_164502.log
```

### 3. View Specific Data

```bash
# Check TPMS (tire pressure & temperature)
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --id 0x4A7

# Check engine RPM
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --id 0x1D0

# Export all decoded data to CSV
./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --export
```

## Analysis Scripts

| Script | Purpose | Best For |
|--------|---------|----------|
| **scripts/decode_with_obdb.py** ⭐ | OBDb-integrated decoder | Getting accurate physical values (PSI, °C, RPM, etc.) |
| **scripts/validate_can.py** | Message validation | Checking capture quality and statistics |
| **scripts/find_tpms.py** | TPMS message finder | Searching for tire pressure data |
| **scripts/decode_can.py** | Pattern analyzer | Analyzing unknown CAN IDs |
| **scripts/analyze_log.sh** | All-in-one analysis | Quick comprehensive overview |

See [SCRIPTS_USAGE.md](SCRIPTS_USAGE.md) for detailed documentation.

## Currently Decoded Signals

The OBDb decoder currently supports:

| CAN ID | Description | Signals |
|--------|-------------|---------|
| 0x0AA | TPMS Pressure | FL, FR, RL, RR tire pressure (psi) |
| 0x0B4 | Vehicle Speed | Speed (km/h) |
| 0x1AA | Steering | Steering angle (degrees) |
| 0x1C4 | Throttle | Throttle position (%) |
| 0x1D0 | Engine | RPM (rpm) |
| 0x2C1 | Engine | Coolant temperature (°C) |
| 0x4A7 | TPMS Temp | Tire temperatures (°C) |
| 0x498 | TPMS/Vehicle | Rear tire pressure (kPa, candidate) |
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
│   ├── SCRIPTS_USAGE.md         # Detailed script documentation
│   ├── 4RUNNER_CAN_ANALYSIS_SUMMARY.md
│   └── TPMS_DECODING_NOTES.md
├── main/                        # ESP32 firmware
├── logs/                        # Captured CAN logs
├── scripts/                     # Python analysis tools
│   ├── decode_with_obdb.py      # ⭐ OBDb-integrated decoder
│   ├── validate_can.py          # CAN validation
│   ├── find_tpms.py             # TPMS finder
│   ├── decode_can.py            # Pattern analyzer
│   ├── calibrate_tpms.py
│   ├── can_decoder.py
│   ├── find_rear_tpms.py
│   └── investigate_tpms.py
├── scripts/analyze_log.sh       # All-in-one analysis
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
