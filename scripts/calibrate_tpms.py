#!/usr/bin/env python3
"""
TPMS Calibration Tool
Helps determine the correct decoding formula based on known tire pressure
"""

import re
import sys
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


def main():
    if len(sys.argv) < 2:
        print("Usage: calibrate_tpms.py <log_file>")
        print("\nThis tool helps calibrate TPMS decoding based on known tire pressure.")
        print("You'll be asked for your actual tire pressure to find the correct formula.")
        sys.exit(1)

    log_file = Path(sys.argv[1])

    # Collect all 0x4A7 messages
    messages = []
    with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            msg = parse_can_message(line)
            if msg and msg['id'] == 0x4A7:
                messages.append(msg)

    if not messages:
        print("No 0x4A7 messages found!")
        sys.exit(1)

    print("=" * 80)
    print("TPMS CALIBRATION TOOL")
    print("=" * 80)
    print(f"\nFound {len(messages)} TPMS messages (ID 0x4A7)")

    # Get known tire pressures from user
    print("\n📋 Enter your known tire pressures (all in PSI):")
    try:
        fl_psi = float(input("  Front Left (FL):  "))
        fr_psi = float(input("  Front Right (FR): "))
        rl_psi = float(input("  Rear Left (RL):   "))
        rr_psi = float(input("  Rear Right (RR):  "))
    except (ValueError, EOFError):
        print("\nError: Please enter valid numbers")
        sys.exit(1)

    # Analyze typical message
    print("\n" + "=" * 80)
    print("ANALYZING TYPICAL MESSAGE")
    print("=" * 80)

    # Use median message to avoid outliers
    mid_msg = messages[len(messages) // 2]
    data = mid_msg['data']

    print(f"\nTypical message: {' '.join(f'{b:02X}' for b in data)}")
    print(f"Decimal values:  {' '.join(f'{b:3d}' for b in data)}")

    # Test different byte position hypotheses
    print("\n" + "=" * 80)
    print("TESTING DECODING HYPOTHESES")
    print("=" * 80)

    hypotheses = [
        {
            'name': 'Layout 1: Bytes 0-3 are FL,FR,RL,RR pressure',
            'positions': {'FL': 0, 'FR': 1, 'RL': 2, 'RR': 3}
        },
        {
            'name': 'Layout 2: Only bytes 0-1 are FL,FR (front only)',
            'positions': {'FL': 0, 'FR': 1, 'RL': None, 'RR': None}
        },
        {
            'name': 'Layout 3: Interleaved (FL,FR,RL,RR across all 8 bytes)',
            'positions': {'FL': 0, 'FR': 2, 'RL': 4, 'RR': 6}
        },
    ]

    known_pressures = {'FL': fl_psi, 'FR': fr_psi, 'RL': rl_psi, 'RR': rr_psi}

    for hyp in hypotheses:
        print(f"\n{hyp['name']}:")
        print("-" * 80)

        valid = True
        formulas = {}

        for wheel, byte_pos in hyp['positions'].items():
            if byte_pos is None:
                print(f"  {wheel}: Not in this layout")
                continue

            if byte_pos >= len(data):
                print(f"  {wheel}: Byte position {byte_pos} out of range")
                valid = False
                continue

            raw_value = data[byte_pos]
            known_psi = known_pressures[wheel]

            # Calculate what formula would work
            if raw_value == 0:
                print(f"  {wheel}: Byte {byte_pos} is 0 (can't calculate)")
                valid = False
                continue

            # Try to find a simple formula
            # Method 1: Linear scaling (x * factor)
            factor = known_psi / raw_value

            # Method 2: With offset (x + offset) * factor
            # Assume the formula might be: (raw + offset) / divisor

            print(f"  {wheel}: Byte {byte_pos} = {raw_value} (0x{raw_value:02X})")
            print(f"         Known pressure: {known_psi} PSI")
            print(f"         Simple formula: x * {factor:.4f} = {raw_value * factor:.2f} PSI")

            # Try common formula patterns
            kpa_value = known_psi * 6.895  # Convert PSI to kPa
            print(f"         If kPa: x * {kpa_value/raw_value:.4f} = {raw_value * kpa_value/raw_value:.1f} kPa")

            # Try with offset patterns
            for offset in [-0.5, -1, -2, -5, -10]:
                if raw_value + offset > 0:
                    factor_with_offset = known_psi / (raw_value + offset)
                    result = (raw_value + offset) * factor_with_offset
                    if abs(result - known_psi) < 0.1:
                        print(f"         With offset: (x {offset:+.1f}) * {factor_with_offset:.4f} = {result:.2f} PSI ✓")

            formulas[wheel] = factor

        if valid:
            print(f"\n  ✓ This layout might work!")
            avg_factor = sum(formulas.values()) / len(formulas) if formulas else 0
            print(f"  Average scaling factor: {avg_factor:.4f}")

            # Show what all 4 bytes would decode to with this formula
            print(f"\n  Decoded values using average factor:")
            for wheel, byte_pos in hyp['positions'].items():
                if byte_pos is not None and byte_pos < len(data):
                    decoded = data[byte_pos] * avg_factor
                    print(f"    {wheel}: {data[byte_pos]:3d} * {avg_factor:.4f} = {decoded:.2f} PSI")

    # Summary
    print("\n" + "=" * 80)
    print("RECOMMENDATION")
    print("=" * 80)

    print("\nBased on your known pressures, look for the layout where:")
    print("  1. The calculated formulas are similar for all wheels")
    print("  2. The decoded values match your known pressures")
    print("  3. The formula is simple (e.g., x * constant)")

    print("\nIf none of the layouts work well:")
    print("  - The rear tires might be on a different CAN ID")
    print("  - The message might contain only front tire data")
    print("  - The encoding might use 16-bit values instead of 8-bit")

    print("\n" + "=" * 80)


if __name__ == '__main__':
    main()
