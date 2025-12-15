#!/usr/bin/env python3
"""
CAN Data Decoder
Decodes and analyzes specific CAN IDs from Toyota 4Runner
"""

import re
import sys
import csv
import struct
from collections import defaultdict, Counter
from pathlib import Path


class CANDecoder:
    # Known CAN IDs and their descriptions for Toyota 4Runner
    KNOWN_IDS = {
        # Common Toyota CAN IDs
        0x020: "Cruise Control",
        0x024: "Steering Wheel Controls",
        0x025: "Multi-function Switch",
        0x0AA: "Wheel Speed (Common)",
        0x0B4: "Vehicle Speed",
        0x0BA: "Brake Pedal",
        0x1AA: "Steering Angle",
        0x1C4: "Throttle Position",
        0x1D0: "Engine Data 1",
        0x223: "Gear Position",
        0x228: "Transmission",
        0x237: "Unknown 1",
        0x2C1: "Engine Data 2",
        0x2C6: "Climate Control 1",
        0x2C7: "Climate Control 2",
        0x2E1: "Unknown 2",
        0x320: "Unknown 3",
        0x340: "Unknown 4",
        0x387: "Unknown 5",
        0x389: "Unknown 6",
        0x38E: "Unknown 7",
        0x3D3: "Unknown 8",
        0x3F9: "Unknown 9",
        0x434: "Unknown 10",
        0x443: "Unknown 11",
        0x498: "Unknown 12",
        0x49C: "Unknown 13",
        0x4A7: "TPMS Data",
        0x4C6: "Unknown 14",
        0x613: "Unknown 15",
        0x638: "Unknown 16",
        0x63B: "Unknown 17",
        0x6D0: "Unknown 18",
    }

    def __init__(self, log_file):
        self.log_file = Path(log_file)
        self.messages_by_id = defaultdict(list)

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

    def decode_wheel_speed(self, data):
        """Decode wheel speed from 0x0AA (tentative)"""
        if len(data) < 8:
            return None

        # Common format: 4 x 16-bit values for each wheel
        fl = (data[0] << 8) | data[1]
        fr = (data[2] << 8) | data[3]
        rl = (data[4] << 8) | data[5]
        rr = (data[6] << 8) | data[7]

        return {
            'FL': fl,
            'FR': fr,
            'RL': rl,
            'RR': rr,
            'unit': 'raw (needs calibration)'
        }

    def decode_vehicle_speed(self, data):
        """Decode vehicle speed from 0x0B4 (tentative)"""
        if len(data) < 2:
            return None

        # Common format: 16-bit value
        speed_raw = (data[0] << 8) | data[1]

        return {
            'speed_raw': speed_raw,
            'speed_kph_estimate': speed_raw / 100.0,  # Common scaling
            'unit': 'estimated (needs calibration)'
        }

    def decode_steering_angle(self, data):
        """Decode steering angle from 0x1AA (tentative)"""
        if len(data) < 4:
            return None

        # Common format: signed 16-bit value
        angle_raw = (data[0] << 8) | data[1]

        # Convert to signed
        if angle_raw > 0x7FFF:
            angle_raw -= 0x10000

        return {
            'angle_raw': angle_raw,
            'angle_deg_estimate': angle_raw / 10.0,  # Common scaling
            'unit': 'estimated (needs calibration)'
        }

    def decode_engine_rpm(self, data):
        """Decode engine RPM from 0x1D0 or similar (tentative)"""
        if len(data) < 3:
            return None

        # Common format: RPM in bytes 1-2
        rpm_raw = (data[1] << 8) | data[2]

        return {
            'rpm_raw': rpm_raw,
            'rpm_estimate': rpm_raw / 4.0,  # Common scaling
            'unit': 'estimated (needs calibration)'
        }

    def decode_message(self, can_id, data):
        """Decode a message based on CAN ID"""
        decoders = {
            0x0AA: self.decode_wheel_speed,
            0x0B4: self.decode_vehicle_speed,
            0x1AA: self.decode_steering_angle,
            0x1D0: self.decode_engine_rpm,
        }

        if can_id in decoders:
            return decoders[can_id](data)

        return None

    def analyze(self):
        """Analyze the log file"""
        print(f"Decoding CAN messages from: {self.log_file}")
        print("=" * 60)

        if not self.log_file.exists():
            print(f"❌ Error: File not found: {self.log_file}")
            return False

        if self.log_file.suffix.lower() == '.bin':
            for msg in self.parse_bin_records():
                self.messages_by_id[msg['id']].append(msg)
        else:
            with open(self.log_file, 'r', encoding='utf-8', errors='ignore') as f:
                first_line = f.readline()
                f.seek(0)

                if first_line.lower().startswith('timestamp_us'):
                    reader = csv.DictReader(f)
                    for row in reader:
                        msg = self.parse_csv_row(row)
                        if msg:
                            self.messages_by_id[msg['id']].append(msg)
                else:
                    for line in f:
                        if 'ID:' not in line:
                            continue

                        msg = self.parse_can_message(line)
                        if msg:
                            self.messages_by_id[msg['id']].append(msg)

        return True

    def print_summary(self):
        """Print summary of all CAN IDs found"""
        print(f"\n📋 CAN ID SUMMARY")
        print("=" * 60)
        print(f"{'CAN ID':<10} {'Count':<10} {'Description':<30} {'DLC'}")
        print("-" * 60)

        for can_id in sorted(self.messages_by_id.keys()):
            messages = self.messages_by_id[can_id]
            count = len(messages)
            description = self.KNOWN_IDS.get(can_id, "Unknown")

            # Get most common DLC
            dlc_counter = Counter(msg['dlc'] for msg in messages)
            common_dlc = dlc_counter.most_common(1)[0][0]

            print(f"0x{can_id:03X}     {count:<10,} {description:<30} {common_dlc}")

        print("=" * 60)

    def print_detailed_analysis(self, can_id):
        """Print detailed analysis for a specific CAN ID"""
        if can_id not in self.messages_by_id:
            print(f"❌ CAN ID 0x{can_id:03X} not found in log")
            return

        messages = self.messages_by_id[can_id]
        description = self.KNOWN_IDS.get(can_id, "Unknown")

        print(f"\n📊 DETAILED ANALYSIS: 0x{can_id:03X} - {description}")
        print("=" * 60)
        print(f"Total messages: {len(messages):,}")

        # Analyze data patterns
        unique_patterns = {}
        for msg in messages:
            data_tuple = tuple(msg['data'])
            if data_tuple not in unique_patterns:
                unique_patterns[data_tuple] = 0
            unique_patterns[data_tuple] += 1

        print(f"Unique patterns: {len(unique_patterns)}")

        # Show most common patterns
        print(f"\n🔝 Most Common Patterns (top 10):")
        sorted_patterns = sorted(unique_patterns.items(), key=lambda x: x[1], reverse=True)

        for i, (pattern, count) in enumerate(sorted_patterns[:10], 1):
            data_str = ' '.join(f"{b:02X}" for b in pattern)
            pct = (count / len(messages)) * 100
            print(f"  {i:2d}. {data_str:<24} (count: {count:5,}, {pct:5.2f}%)")

            # Try to decode if decoder available
            decoded = self.decode_message(can_id, list(pattern))
            if decoded:
                print(f"      Decoded: {decoded}")

        # Show byte variation analysis
        print(f"\n📈 Byte Variation Analysis:")
        if len(messages) > 0:
            max_dlc = max(msg['dlc'] for msg in messages)

            for byte_idx in range(max_dlc):
                byte_values = []
                for msg in messages:
                    if byte_idx < len(msg['data']):
                        byte_values.append(msg['data'][byte_idx])

                if byte_values:
                    unique_vals = len(set(byte_values))
                    min_val = min(byte_values)
                    max_val = max(byte_values)
                    avg_val = sum(byte_values) / len(byte_values)

                    print(f"  Byte {byte_idx}: min=0x{min_val:02X} max=0x{max_val:02X} "
                          f"avg=0x{int(avg_val):02X} unique={unique_vals}")

        # Show temporal variation
        if len(messages) >= 3:
            print(f"\n⏱️  Temporal Variation:")
            print(f"  First:  {' '.join(f'{b:02X}' for b in messages[0]['data'])}")
            print(f"  Middle: {' '.join(f'{b:02X}' for b in messages[len(messages)//2]['data'])}")
            print(f"  Last:   {' '.join(f'{b:02X}' for b in messages[-1]['data'])}")

        print("=" * 60)

    def export_decoded(self, output_file=None):
        """Export decoded data to CSV"""
        if output_file is None:
            output_file = self.log_file.parent / f"{self.log_file.stem}_decoded.csv"

        with open(output_file, 'w') as f:
            f.write("CAN_ID,Message_Index,Data_Hex,Decoded\n")

            for can_id in sorted(self.messages_by_id.keys()):
                messages = self.messages_by_id[can_id]

                for idx, msg in enumerate(messages):
                    data_str = ' '.join(f"{b:02X}" for b in msg['data'])
                    decoded = self.decode_message(can_id, msg['data'])
                    decoded_str = str(decoded) if decoded else ""

                    f.write(f"0x{can_id:03X},{idx},{data_str},{decoded_str}\n")

        print(f"\n💾 Decoded data exported to: {output_file}")


def main():
    if len(sys.argv) < 2:
        print("Usage: scripts/decode_can.py <log_file> [--id 0xXXX] [--export]")
        print("\nExamples:")
        print("  ./scripts/decode_can.py logs/can_capture_20251212_164502.log")
        print("  ./scripts/decode_can.py logs/can_capture_20251212_164502.log --id 0x0AA")
        print("  ./scripts/decode_can.py logs/can_capture_20251212_164502.log --export")
        sys.exit(1)

    log_file = sys.argv[1]

    # Parse options
    specific_id = None
    export = '--export' in sys.argv

    for i, arg in enumerate(sys.argv):
        if arg == '--id' and i + 1 < len(sys.argv):
            try:
                specific_id = int(sys.argv[i + 1], 16)
            except ValueError:
                print(f"❌ Invalid CAN ID: {sys.argv[i + 1]}")
                sys.exit(1)

    decoder = CANDecoder(log_file)
    if decoder.analyze():
        if specific_id is not None:
            decoder.print_detailed_analysis(specific_id)
        else:
            decoder.print_summary()

        if export:
            decoder.export_decoded()


if __name__ == '__main__':
    main()
