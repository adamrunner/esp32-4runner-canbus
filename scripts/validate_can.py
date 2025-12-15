#!/usr/bin/env python3
"""
CAN Log Validator
Validates CAN message captures and provides statistics
"""

import re
import sys
import csv
import struct
from collections import defaultdict, Counter
from pathlib import Path


class CANValidator:
    def __init__(self, log_file):
        self.log_file = Path(log_file)
        self.messages = []
        self.stats = {
            'total_lines': 0,
            'valid_can_messages': 0,
            'invalid_messages': 0,
            'unique_ids': set(),
            'id_counts': Counter(),
            'dlc_distribution': Counter(),
            'parse_errors': []
        }

    def parse_can_message(self, line):
        """Parse a CAN message line from the log"""
        # Pattern matches both raw and cleaned log formats
        # Example: "I (123) TAG: ID: 0x123 DLC: 8 Data: 00 11 22 33 44 55 66 77"
        pattern = r'ID:\s*0x([0-9A-Fa-f]+)\s+DLC:\s*(\d+)\s+Data:\s*([0-9A-Fa-f\s]+)'

        match = re.search(pattern, line)
        if match:
            can_id = int(match.group(1), 16)
            dlc = int(match.group(2))
            data_str = match.group(3).strip()
            data_bytes = [int(b, 16) for b in data_str.split()[:8]]  # Max 8 bytes

            # Validate
            if dlc > 8:
                return None, f"Invalid DLC: {dlc}"

            if len(data_bytes) < dlc:
                return None, f"Data length mismatch: DLC={dlc}, got {len(data_bytes)} bytes"

            return {
                'id': can_id,
                'dlc': dlc,
                'data': data_bytes[:dlc],  # Only keep DLC bytes
                'raw_line': line.strip()
            }, None

        return None, "No CAN pattern match"

    def parse_csv_row(self, row):
        try:
            can_id = int(str(row.get('can_id', '')).strip(), 16)
            dlc = int(row.get('dlc', 0))
        except Exception:
            return None, "CSV parse error"

        data_bytes = []
        for i in range(8):
            val = str(row.get(f'byte{i}', '')).strip()
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

        if dlc > 8:
            return None, f"Invalid DLC: {dlc}"

        return {
            'id': can_id,
            'dlc': dlc,
            'data': data_bytes[:dlc],
            'raw_line': None
        }, None

    def parse_bin_records(self):
        record_struct = struct.Struct('<QHB8B')
        with open(self.log_file, 'rb') as f:
            while True:
                chunk = f.read(record_struct.size)
                if len(chunk) < record_struct.size:
                    break
                _, can_id, dlc, *data_bytes = record_struct.unpack(chunk)
                msg = {
                    'id': can_id,
                    'dlc': dlc,
                    'data': data_bytes[:dlc],
                    'raw_line': None
                }
                yield msg, None

    def validate(self):
        """Validate the log file"""
        print(f"Validating: {self.log_file}")
        print("=" * 60)

        if not self.log_file.exists():
            print(f"❌ Error: File not found: {self.log_file}")
            return False

        if self.log_file.suffix.lower() == '.bin':
            for line_num, (msg, error) in enumerate(self.parse_bin_records(), 1):
                self.stats['total_lines'] += 1
                if msg:
                    self.messages.append(msg)
                    self.stats['valid_can_messages'] += 1
                    self.stats['unique_ids'].add(msg['id'])
                    self.stats['id_counts'][msg['id']] += 1
                    self.stats['dlc_distribution'][msg['dlc']] += 1
                else:
                    self.stats['invalid_messages'] += 1
                    if len(self.stats['parse_errors']) < 10:
                        self.stats['parse_errors'].append((line_num, error, None))
        else:
            with open(self.log_file, 'r', encoding='utf-8', errors='ignore') as f:
                first_line = f.readline()
                f.seek(0)

                if first_line.lower().startswith('timestamp_us'):
                    reader = csv.DictReader(f)
                    for line_num, row in enumerate(reader, 1):
                        self.stats['total_lines'] += 1
                        msg, error = self.parse_csv_row(row)

                        if msg:
                            self.messages.append(msg)
                            self.stats['valid_can_messages'] += 1
                            self.stats['unique_ids'].add(msg['id'])
                            self.stats['id_counts'][msg['id']] += 1
                            self.stats['dlc_distribution'][msg['dlc']] += 1
                        else:
                            self.stats['invalid_messages'] += 1
                            if len(self.stats['parse_errors']) < 10:
                                self.stats['parse_errors'].append((line_num, error, row))
                else:
                    for line_num, line in enumerate(f, 1):
                        self.stats['total_lines'] += 1

                        # Skip empty lines and ESP-IDF boot messages
                        if not line.strip() or 'ID:' not in line:
                            continue

                        msg, error = self.parse_can_message(line)

                        if msg:
                            self.messages.append(msg)
                            self.stats['valid_can_messages'] += 1
                            self.stats['unique_ids'].add(msg['id'])
                            self.stats['id_counts'][msg['id']] += 1
                            self.stats['dlc_distribution'][msg['dlc']] += 1
                        elif 'ID:' in line and 'DLC:' in line:  # Looks like CAN but failed to parse
                            self.stats['invalid_messages'] += 1
                            if len(self.stats['parse_errors']) < 10:  # Keep first 10 errors
                                self.stats['parse_errors'].append((line_num, error, line.strip()))

        return True

    def print_report(self):
        """Print validation report"""
        print(f"\n📊 VALIDATION REPORT")
        print("=" * 60)

        # Overall stats
        print(f"Total lines:          {self.stats['total_lines']:,}")
        print(f"Valid CAN messages:   {self.stats['valid_can_messages']:,}")
        print(f"Invalid messages:     {self.stats['invalid_messages']}")
        print(f"Unique CAN IDs:       {len(self.stats['unique_ids'])}")

        if self.stats['valid_can_messages'] > 0:
            print(f"\n✅ CAN messages detected and validated!")

            # DLC Distribution
            print(f"\n📏 DLC Distribution:")
            for dlc in sorted(self.stats['dlc_distribution'].keys()):
                count = self.stats['dlc_distribution'][dlc]
                pct = (count / self.stats['valid_can_messages']) * 100
                print(f"  DLC {dlc}: {count:6,} messages ({pct:5.2f}%)")

            # Top 10 most frequent IDs
            print(f"\n🔝 Top 10 Most Frequent CAN IDs:")
            for can_id, count in self.stats['id_counts'].most_common(10):
                pct = (count / self.stats['valid_can_messages']) * 100
                print(f"  0x{can_id:03X}: {count:6,} messages ({pct:5.2f}%)")

            # Show first few messages
            print(f"\n📝 Sample Messages (first 5):")
            for msg in self.messages[:5]:
                data_str = ' '.join(f"{b:02X}" for b in msg['data'])
                print(f"  ID: 0x{msg['id']:03X}  DLC: {msg['dlc']}  Data: {data_str}")
        else:
            print(f"\n❌ No valid CAN messages found!")

        # Parse errors
        if self.stats['parse_errors']:
            print(f"\n⚠️  Parse Errors (showing first 10):")
            for line_num, error, line in self.stats['parse_errors']:
                print(f"  Line {line_num}: {error}")
                print(f"    {line[:100]}...")

        print("=" * 60)


def main():
    if len(sys.argv) < 2:
        print("Usage: scripts/validate_can.py <log_file>")
        print("\nExample:")
        print("  ./scripts/validate_can.py logs/can_capture_20251212_164502.log")
        sys.exit(1)

    log_file = sys.argv[1]

    validator = CANValidator(log_file)
    if validator.validate():
        validator.print_report()

        # Exit code based on validation
        if validator.stats['valid_can_messages'] > 0:
            sys.exit(0)
        else:
            sys.exit(1)


if __name__ == '__main__':
    main()
