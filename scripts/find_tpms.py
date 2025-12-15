#!/usr/bin/env python3
"""
TPMS Finder and Decoder
Searches for TPMS (Tire Pressure Monitoring System) messages in CAN logs
"""

import re
import sys
import csv
import struct
from collections import defaultdict
from pathlib import Path


class TPMSFinder:
    # Common TPMS CAN IDs for Toyota vehicles
    KNOWN_TPMS_IDS = {
        0x4A7: "TPMS Data (Common)",
        0x4A8: "TPMS Data Alt 1",
        0x4A9: "TPMS Data Alt 2",
        0x4AA: "TPMS Data Alt 3",
        0x750: "TPMS Request/Response",
    }

    def __init__(self, log_file):
        self.log_file = Path(log_file)
        self.tpms_messages = defaultdict(list)
        self.all_messages_by_id = defaultdict(list)

    def parse_can_message(self, line):
        """Parse a CAN message line from the log"""
        pattern = r'ID:\s*0x([0-9A-Fa-f]+)\s+DLC:\s*(\d+)\s+Data:\s*([0-9A-Fa-f\s]+)'

        match = re.search(pattern, line)
        if match:
            can_id = int(match.group(1), 16)
            dlc = int(match.group(2))
            data_str = match.group(3).strip()
            data_bytes = [int(b, 16) for b in data_str.split()[:8]]

            return {
                'id': can_id,
                'dlc': dlc,
                'data': data_bytes[:dlc]
            }
        return None

    def parse_csv_row(self, row):
        try:
            can_id = int(str(row.get('can_id', '')).strip(), 16)
            dlc = int(row.get('dlc', 0))
        except Exception:
            return None

        data_bytes = []
        for i in range(8):
            val = row.get(f'byte{i}', '').strip()
            if val == '':
                data_bytes.append(0)
                continue
            try:
                data_bytes.append(int(val, 16))
            except ValueError:
                try:
                    data_bytes.append(int(val))
                except ValueError:
                    data_bytes.append(0)

        return {
            'id': can_id,
            'dlc': dlc,
            'data': data_bytes[:dlc]
        }

    def parse_bin_records(self):
        record_struct = struct.Struct('<QHB8B')
        with open(self.log_file, 'rb') as f:
            while True:
                chunk = f.read(record_struct.size)
                if len(chunk) < record_struct.size:
                    break
                _, can_id, dlc, *data_bytes = record_struct.unpack(chunk)
                yield {
                    'id': can_id,
                    'dlc': dlc,
                    'data': data_bytes[:dlc]
                }

    def decode_tpms_0x4a7(self, data):
        """
        Decode TPMS data from 0x4A7 (common Toyota TPMS format)
        This is a heuristic decoder - actual format may vary
        """
        if len(data) < 8:
            return None

        try:
            # Common TPMS format (may need adjustment based on actual vehicle):
            # Bytes 0-1: Front Left pressure (in some unit)
            # Bytes 2-3: Front Right pressure
            # Bytes 4-5: Rear Left pressure
            # Bytes 6-7: Rear Right pressure
            # OR could be: pressure values in different encoding

            # Try interpreting as raw values
            fl = data[0]
            fr = data[1]
            rl = data[2]
            rr = data[3]

            # Alternative: 16-bit values
            fl_16 = (data[0] << 8) | data[1]
            fr_16 = (data[2] << 8) | data[3]
            rl_16 = (data[4] << 8) | data[5]
            rr_16 = (data[6] << 8) | data[7]

            return {
                'raw_bytes': data,
                '8bit_values': {'FL': fl, 'FR': fr, 'RL': rl, 'RR': rr},
                '16bit_values': {'FL': fl_16, 'FR': fr_16, 'RL': rl_16, 'RR': rr_16},
            }
        except:
            return None

    def analyze(self):
        """Analyze the log file for TPMS messages"""
        print(f"Searching for TPMS messages in: {self.log_file}")
        print("=" * 60)

        if not self.log_file.exists():
            print(f"❌ Error: File not found: {self.log_file}")
            return False

        if self.log_file.suffix.lower() == '.bin':
            for msg in self.parse_bin_records():
                self.all_messages_by_id[msg['id']].append(msg)
                if msg['id'] in self.KNOWN_TPMS_IDS:
                    self.tpms_messages[msg['id']].append(msg)
        else:
            with open(self.log_file, 'r', encoding='utf-8', errors='ignore') as f:
                first_line = f.readline()
                f.seek(0)

                if first_line.lower().startswith('timestamp_us'):
                    reader = csv.DictReader(f)
                    for row in reader:
                        msg = self.parse_csv_row(row)
                        if not msg:
                            continue

                        self.all_messages_by_id[msg['id']].append(msg)
                        if msg['id'] in self.KNOWN_TPMS_IDS:
                            self.tpms_messages[msg['id']].append(msg)
                else:
                    for line in f:
                        if 'ID:' not in line:
                            continue

                        msg = self.parse_can_message(line)
                        if not msg:
                            continue

                        self.all_messages_by_id[msg['id']].append(msg)

                        if msg['id'] in self.KNOWN_TPMS_IDS:
                            self.tpms_messages[msg['id']].append(msg)

        return True

    def print_report(self):
        """Print TPMS analysis report"""
        print(f"\n🔍 TPMS SEARCH REPORT")
        print("=" * 60)

        # Report on known TPMS IDs
        print("\n📡 Known TPMS IDs:")
        found_tpms = False
        for can_id, description in sorted(self.KNOWN_TPMS_IDS.items()):
            count = len(self.tpms_messages.get(can_id, []))
            if count > 0:
                print(f"  ✅ 0x{can_id:03X} ({description}): {count} messages")
                found_tpms = True
            else:
                print(f"  ❌ 0x{can_id:03X} ({description}): Not found")

        if not found_tpms:
            print("\n⚠️  No known TPMS IDs found!")
            print("\n💡 Potential TPMS IDs (searching for patterns):")

            # Look for IDs in the typical TPMS range (0x4A0-0x4AF, 0x750-0x760)
            potential_ids = []
            for can_id, messages in self.all_messages_by_id.items():
                if (0x4A0 <= can_id <= 0x4AF) or (0x750 <= can_id <= 0x760):
                    potential_ids.append((can_id, len(messages)))

            if potential_ids:
                for can_id, count in sorted(potential_ids):
                    print(f"  🔍 0x{can_id:03X}: {count} messages")
                    # Show sample
                    sample = self.all_messages_by_id[can_id][0]
                    data_str = ' '.join(f"{b:02X}" for b in sample['data'])
                    print(f"      Sample: {data_str}")
            else:
                print("  No messages found in typical TPMS ID ranges")

        # Decode known TPMS messages
        if 0x4A7 in self.tpms_messages:
            print(f"\n📊 Decoding 0x4A7 TPMS Messages:")
            print("-" * 60)

            messages = self.tpms_messages[0x4A7]
            print(f"Total messages: {len(messages)}")

            # Show unique data patterns
            unique_patterns = {}
            for msg in messages:
                data_tuple = tuple(msg['data'])
                if data_tuple not in unique_patterns:
                    unique_patterns[data_tuple] = 0
                unique_patterns[data_tuple] += 1

            print(f"Unique patterns: {len(unique_patterns)}")
            print(f"\nMost common patterns (top 5):")

            sorted_patterns = sorted(unique_patterns.items(), key=lambda x: x[1], reverse=True)
            for i, (pattern, count) in enumerate(sorted_patterns[:5], 1):
                data_str = ' '.join(f"{b:02X}" for b in pattern)
                decoded = self.decode_tpms_0x4a7(list(pattern))

                print(f"\n  Pattern {i}: {data_str} (count: {count})")
                if decoded:
                    print(f"    8-bit interpretation:  FL={decoded['8bit_values']['FL']:3d} "
                          f"FR={decoded['8bit_values']['FR']:3d} "
                          f"RL={decoded['8bit_values']['RL']:3d} "
                          f"RR={decoded['8bit_values']['RR']:3d}")
                    print(f"    16-bit interpretation: FL={decoded['16bit_values']['FL']:5d} "
                          f"FR={decoded['16bit_values']['FR']:5d} "
                          f"RL={decoded['16bit_values']['RL']:5d} "
                          f"RR={decoded['16bit_values']['RR']:5d}")

            # Show data change over time
            if len(messages) > 1:
                print(f"\n  Data variation analysis:")
                print(f"    First message: {' '.join(f'{b:02X}' for b in messages[0]['data'])}")
                print(f"    Last message:  {' '.join(f'{b:02X}' for b in messages[-1]['data'])}")

        print("\n" + "=" * 60)

    def export_tpms_messages(self, output_file=None):
        """Export TPMS messages to a file"""
        if not self.tpms_messages:
            print("No TPMS messages to export")
            return

        if output_file is None:
            output_file = self.log_file.parent / f"{self.log_file.stem}_tpms.txt"

        with open(output_file, 'w') as f:
            f.write("TPMS Messages Export\n")
            f.write("=" * 60 + "\n\n")

            for can_id, messages in sorted(self.tpms_messages.items()):
                f.write(f"CAN ID: 0x{can_id:03X} - {self.KNOWN_TPMS_IDS.get(can_id, 'Unknown')}\n")
                f.write(f"Total messages: {len(messages)}\n")
                f.write("-" * 60 + "\n")

                for i, msg in enumerate(messages, 1):
                    data_str = ' '.join(f"{b:02X}" for b in msg['data'])
                    f.write(f"{i:5d}: {data_str}\n")

                f.write("\n")

        print(f"\n💾 TPMS messages exported to: {output_file}")


def main():
    if len(sys.argv) < 2:
        print("Usage: scripts/find_tpms.py <log_file> [--export]")
        print("\nExample:")
        print("  ./scripts/find_tpms.py logs/can_capture_20251212_164502.log")
        print("  ./scripts/find_tpms.py logs/can_capture_20251212_164502.log --export")
        sys.exit(1)

    log_file = sys.argv[1]
    export = '--export' in sys.argv

    finder = TPMSFinder(log_file)
    if finder.analyze():
        finder.print_report()

        if export:
            finder.export_tpms_messages()


if __name__ == '__main__':
    main()
