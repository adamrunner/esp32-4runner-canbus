#!/usr/bin/env python3
"""
Find Rear TPMS Data
Searches all CAN IDs for messages with byte values similar to front tire encoding
"""

import re
from pathlib import Path
from collections import defaultdict


def parse_can_message(line):
    pattern = r'ID:\s*0x([0-9A-Fa-f]+)\s+DLC:\s*(\d+)\s+Data:\s*([0-9A-Fa-f\s]+)'
    match = re.search(pattern, line)
    if match:
        can_id = int(match.group(1), 16)
        dlc = int(match.group(2))
        data_str = match.group(3).strip()
        data_bytes = [int(b, 16) for b in data_str.split()[:8]]
        return {'id': can_id, 'dlc': dlc, 'data': data_bytes[:dlc]}
    return None


log_file = Path("logs/can_capture_20251212_164502.log")
messages_by_id = defaultdict(list)

# Collect all messages
with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        msg = parse_can_message(line)
        if msg:
            messages_by_id[msg['id']].append(msg)

print("=" * 80)
print("SEARCHING FOR REAR TPMS DATA")
print("=" * 80)
print("\nFront tires (0x4A7 bytes 0-1) have values ~165-170")
print("Looking for CAN IDs with similar byte patterns...\n")

# Look for IDs with bytes in the range 150-180 (close to front tire values)
target_range = range(150, 181)

candidates = []

for can_id, messages in sorted(messages_by_id.items()):
    # Check if any messages have bytes in our target range
    for msg in messages:
        for byte_idx, byte_val in enumerate(msg['data']):
            if byte_val in target_range:
                candidates.append({
                    'id': can_id,
                    'byte_idx': byte_idx,
                    'value': byte_val,
                    'msg': msg
                })
                break  # Only need one match per ID
        else:
            continue
        break

print(f"Found {len(candidates)} CAN IDs with bytes in range 150-180:\n")

for cand in candidates:
    can_id = cand['id']
    messages = messages_by_id[can_id]

    print(f"\n0x{can_id:03X} ({len(messages)} messages):")

    # Show byte statistics for this ID
    if messages:
        msg_len = len(messages[0]['data'])
        for byte_idx in range(msg_len):
            values = [m['data'][byte_idx] for m in messages if byte_idx < len(m['data'])]
            if values:
                min_val = min(values)
                max_val = max(values)
                avg_val = sum(values) / len(values)

                # Highlight bytes in our target range
                if min_val in target_range or max_val in target_range or avg_val in target_range:
                    print(f"  Byte {byte_idx}: min={min_val:3d} max={max_val:3d} avg={avg_val:6.1f} ⭐")
                else:
                    print(f"  Byte {byte_idx}: min={min_val:3d} max={max_val:3d} avg={avg_val:6.1f}")

    # Show a sample message
    sample = messages[len(messages)//2]
    print(f"  Sample: {' '.join(f'{b:02X}' for b in sample['data'])}")

    # Try decoding with the front tire formula (x * 0.23 PSI)
    print(f"  Decoded (using x * 0.23 PSI):")
    for byte_idx, byte_val in enumerate(sample['data']):
        psi = byte_val * 0.23
        if 30 < psi < 45:  # Reasonable tire pressure range
            print(f"    Byte {byte_idx}: {byte_val} → {psi:.1f} PSI ✓")

print("\n" + "=" * 80)
