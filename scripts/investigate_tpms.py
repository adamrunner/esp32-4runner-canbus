#!/usr/bin/env python3
"""
TPMS Investigation Tool
Analyzes 0x4A7 patterns to determine correct decoding

Expected values:
- All tires: ~38 PSI (2.62 bars)
- Ambient temp: ~55°F (13°C)
"""

import re
import sys
from collections import defaultdict
from pathlib import Path


def parse_can_message(line):
    """Parse a CAN message line"""
    pattern = r'ID:\s*0x([0-9A-Fa-f]+)\s+DLC:\s*(\d+)\s+Data:\s*([0-9A-Fa-f\s]+)'
    match = re.search(pattern, line)
    if match:
        can_id = int(match.group(1), 16)
        dlc = int(match.group(2))
        data_str = match.group(3).strip()
        data_bytes = [int(b, 16) for b in data_str.split()[:8]]
        return {'id': can_id, 'dlc': dlc, 'data': data_bytes[:dlc]}
    return None


def try_decoding_schemes(data_bytes):
    """Try various decoding schemes"""
    schemes = {}

    # Scheme 1: Original (FL, FR, RL, RR in bytes 0-3, temps in 4-7)
    schemes['Bytes 0-3 pressure, 4-7 temp'] = {
        'FL_P': data_bytes[0], 'FR_P': data_bytes[1],
        'RL_P': data_bytes[2], 'RR_P': data_bytes[3],
        'FL_T': data_bytes[4], 'FR_T': data_bytes[5],
        'RL_T': data_bytes[6], 'RR_T': data_bytes[7],
    }

    # Scheme 2: Interleaved (pressure, temp pairs)
    if len(data_bytes) >= 8:
        schemes['Interleaved (P,T pairs)'] = {
            'FL_P': data_bytes[0], 'FL_T': data_bytes[1],
            'FR_P': data_bytes[2], 'FR_T': data_bytes[3],
            'RL_P': data_bytes[4], 'RL_T': data_bytes[5],
            'RR_P': data_bytes[6], 'RR_T': data_bytes[7],
        }

    # Scheme 3: Front only in this message
    schemes['Front only (bytes 0-1 pressure)'] = {
        'FL_P': data_bytes[0], 'FR_P': data_bytes[1],
        'data2': data_bytes[2], 'data3': data_bytes[3],
        'data4': data_bytes[4], 'data5': data_bytes[5],
        'data6': data_bytes[6], 'data7': data_bytes[7],
    }

    # Scheme 4: All pressure in bytes 0-3 (different formula per wheel?)
    schemes['All pressure bytes 0-3'] = {
        'Wheel1_P': data_bytes[0], 'Wheel2_P': data_bytes[1],
        'Wheel3_P': data_bytes[2], 'Wheel4_P': data_bytes[3],
    }

    return schemes


def apply_pressure_formulas(raw_value):
    """Try different pressure conversion formulas"""
    formulas = {}

    # OBDb formula
    formulas['OBDb: (x-0.5)/58 bars'] = f"{((raw_value - 0.5) / 58):.2f} bars ({((raw_value - 0.5) / 58 * 14.5):.1f} PSI)"

    # Simple scaling
    formulas['Simple: x/4 PSI'] = f"{(raw_value / 4):.2f} PSI"
    formulas['Simple: x/5 PSI'] = f"{(raw_value / 5):.2f} PSI"
    formulas['Simple: x*0.2 PSI'] = f"{(raw_value * 0.2):.2f} PSI"

    # With offset
    formulas['Offset: (x-50)/4 PSI'] = f"{((raw_value - 50) / 4):.2f} PSI"
    formulas['Offset: (x-100)/2 PSI'] = f"{((raw_value - 100) / 2):.2f} PSI"

    # Direct kPa
    formulas['Direct: x*2 kPa'] = f"{(raw_value * 2):.0f} kPa ({(raw_value * 2 / 6.895):.1f} PSI)"
    formulas['Direct: x kPa'] = f"{raw_value:.0f} kPa ({(raw_value / 6.895):.1f} PSI)"

    # Try to match ~38 PSI (262 kPa)
    if raw_value > 0:
        target_psi = 38
        scale_factor = target_psi / raw_value
        formulas[f'Match 38 PSI: x*{scale_factor:.3f}'] = f"{(raw_value * scale_factor):.1f} PSI"

    return formulas


def apply_temp_formulas(raw_value):
    """Try different temperature conversion formulas"""
    formulas = {}

    # OBDb formula
    formulas['OBDb: x-40 °C'] = f"{(raw_value - 40):.0f}°C ({((raw_value - 40) * 9/5 + 32):.0f}°F)"

    # Different offsets
    formulas['x-50 °C'] = f"{(raw_value - 50):.0f}°C ({((raw_value - 50) * 9/5 + 32):.0f}°F)"
    formulas['x-30 °C'] = f"{(raw_value - 30):.0f}°C ({((raw_value - 30) * 9/5 + 32):.0f}°F)"

    # Direct
    formulas['Direct x °C'] = f"{raw_value:.0f}°C ({(raw_value * 9/5 + 32):.0f}°F)"

    # Scaled
    formulas['x/2 °C'] = f"{(raw_value / 2):.0f}°C ({((raw_value / 2) * 9/5 + 32):.0f}°F)"

    return formulas


def main():
    if len(sys.argv) < 2:
        print("Usage: investigate_tpms.py <log_file>")
        sys.exit(1)

    log_file = Path(sys.argv[1])

    print("=" * 80)
    print("TPMS INVESTIGATION TOOL")
    print("=" * 80)
    print("\nExpected values:")
    print("  - Tire Pressure: ~38 PSI (2.62 bars, 262 kPa)")
    print("  - Temperature: ~55°F (13°C) ambient")
    print("\n" + "=" * 80)

    # Collect all 0x4A7 messages
    messages_4a7 = []
    with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            msg = parse_can_message(line)
            if msg and msg['id'] == 0x4A7:
                messages_4a7.append(msg)

    print(f"\nFound {len(messages_4a7)} messages on ID 0x4A7")

    if not messages_4a7:
        print("No 0x4A7 messages found!")
        sys.exit(1)

    # Analyze unique patterns
    unique_patterns = {}
    for msg in messages_4a7:
        pattern = tuple(msg['data'])
        if pattern not in unique_patterns:
            unique_patterns[pattern] = 0
        unique_patterns[pattern] += 1

    print(f"Unique patterns: {len(unique_patterns)}")

    # Show byte statistics
    print("\n" + "=" * 80)
    print("BYTE STATISTICS")
    print("=" * 80)

    for byte_idx in range(8):
        values = [msg['data'][byte_idx] for msg in messages_4a7 if byte_idx < len(msg['data'])]
        if values:
            print(f"\nByte {byte_idx}:")
            print(f"  Min: {min(values):3d} (0x{min(values):02X})  " +
                  f"Max: {max(values):3d} (0x{max(values):02X})  " +
                  f"Avg: {sum(values)/len(values):6.1f}  " +
                  f"Unique: {len(set(values))}")

            # If values are stable (low variation), might be temperature
            # If values have high variation, might be pressure
            variation = max(values) - min(values)
            if variation < 20:
                print(f"  → Low variation ({variation}) - possibly temperature or constant")
            else:
                print(f"  → High variation ({variation}) - possibly pressure or dynamic value")

    # Analyze most common pattern in detail
    most_common = sorted(unique_patterns.items(), key=lambda x: x[1], reverse=True)[0]
    pattern, count = most_common

    print("\n" + "=" * 80)
    print(f"MOST COMMON PATTERN (appears {count} times):")
    print("=" * 80)
    print("Raw bytes:", ' '.join(f"{b:02X}" for b in pattern))
    print("Decimal:  ", ' '.join(f"{b:3d}" for b in pattern))

    # Try different decoding schemes
    print("\n" + "=" * 80)
    print("TRYING DIFFERENT BYTE LAYOUTS")
    print("=" * 80)

    schemes = try_decoding_schemes(list(pattern))
    for scheme_name, layout in schemes.items():
        print(f"\n{scheme_name}:")
        for key, val in layout.items():
            print(f"  {key}: {val:3d} (0x{val:02X})")

    # For the most likely candidates (bytes that could be ~38 PSI)
    print("\n" + "=" * 80)
    print("PRESSURE FORMULAS (trying to match ~38 PSI)")
    print("=" * 80)

    for byte_idx in range(min(4, len(pattern))):
        raw = pattern[byte_idx]
        print(f"\nIf Byte {byte_idx} (0x{raw:02X} = {raw}) is pressure:")
        formulas = apply_pressure_formulas(raw)
        for formula_name, result in formulas.items():
            # Highlight if close to 38 PSI
            marker = " ✓" if "38" in result or "37" in result or "39" in result else ""
            print(f"  {formula_name:<30} = {result}{marker}")

    # Temperature analysis
    print("\n" + "=" * 80)
    print("TEMPERATURE FORMULAS (trying to match ~55°F / 13°C)")
    print("=" * 80)

    for byte_idx in range(4, min(8, len(pattern))):
        raw = pattern[byte_idx]
        print(f"\nIf Byte {byte_idx} (0x{raw:02X} = {raw}) is temperature:")
        formulas = apply_temp_formulas(raw)
        for formula_name, result in formulas.items():
            # Highlight if close to ambient
            marker = " ✓" if any(x in result for x in ["55°F", "13°C", "54°F", "56°F", "12°C", "14°C"]) else ""
            print(f"  {formula_name:<30} = {result}{marker}")

    # Show variation over time
    print("\n" + "=" * 80)
    print("VARIATION OVER TIME")
    print("=" * 80)

    print("\nFirst 5 messages:")
    for i, msg in enumerate(messages_4a7[:5], 1):
        print(f"  {i}. {' '.join(f'{b:02X}' for b in msg['data'])}")

    print("\nLast 5 messages:")
    for i, msg in enumerate(messages_4a7[-5:], 1):
        print(f"  {i}. {' '.join(f'{b:02X}' for b in msg['data'])}")

    print("\n" + "=" * 80)


if __name__ == '__main__':
    main()
