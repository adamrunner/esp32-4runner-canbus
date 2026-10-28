#!/usr/bin/env python3
"""
Enhanced CAN Decoder with OBDb Integration
Decodes CAN messages using Toyota 4Runner OBDb database
"""

import re
import sys
import json
import csv
import struct
from collections import defaultdict, Counter
from pathlib import Path


class OBDbDecoder:
    def __init__(self, log_file, obdb_path=None):
        self.log_file = Path(log_file)
        self.messages_by_id = defaultdict(list)
        self.obdb_data = None
        self.obdb_path = Path(obdb_path) if obdb_path else Path.home() / "Code/OBDb-Toyota-4Runner/signalsets/v3/default.json"

        # Built-in passive broadcast definitions (listen-only, no OBD-II queries), mirroring
        # dbc/toyota_4runner_2018.dbc; see analysis/QUICK_REFERENCE.md for the evidence.
        # bix counts from the MSB of byte 0, so a DBC Motorola start bit b in byte n is
        # bix = n * 8 + 7 - b % 8. Values are (raw + add) * mul / div.
        self.manual_can_ids = {
            0x024: {
                "name": "Kinematics",
                "signals": [
                    {"name": "Yaw Rate", "bix": 6, "len": 10, "add": -512, "unit": "deg/s"},
                    {"name": "Steering Torque", "bix": 22, "len": 10, "add": -512, "unit": "raw"},
                    # (raw - 512) * -0.002121 - 0.0126, as in the firmware
                    {"name": "Lateral G", "bix": 38, "len": 10, "add": -506.06, "mul": -0.002121, "unit": "g"},
                ],
                "source": "built-in",
            },
            0x025: {
                "name": "Steering Angle Sensor",
                "signals": [
                    {"name": "Steering Angle", "bix": 4, "len": 12, "sign": True, "mul": 1.5, "unit": "degrees"},
                ],
                "source": "built-in",
            },
            # Not TPMS: tire pressure is not on this bus (docs/TPMS_DECODING_NOTES.md)
            0x0AA: {
                "name": "Wheel Speed",
                "signals": [
                    {"name": "FR Speed", "bix": 0, "len": 16, "add": -6767, "div": 100, "unit": "km/h"},
                    {"name": "FL Speed", "bix": 16, "len": 16, "add": -6767, "div": 100, "unit": "km/h"},
                    {"name": "RR Speed", "bix": 32, "len": 16, "add": -6767, "div": 100, "unit": "km/h"},
                    {"name": "RL Speed", "bix": 48, "len": 16, "add": -6767, "div": 100, "unit": "km/h"},
                ],
                "source": "built-in",
            },
            0x0B4: {
                "name": "Vehicle Speed",
                "signals": [
                    {"name": "Speed", "bix": 40, "len": 16, "div": 100, "unit": "km/h"},
                ],
                "source": "built-in",
            },
            0x1C4: {
                "name": "Engine RPM",
                "signals": [
                    {"name": "RPM", "bix": 0, "len": 16, "mul": 0.78125, "unit": "rpm"},
                ],
                "source": "built-in",
            },
            0x224: {
                "name": "Brake Module",
                "signals": [
                    {"name": "Brake Pressed", "bix": 2, "len": 1, "unit": "bool"},
                ],
                "source": "built-in",
            },
            0x2C1: {
                "name": "Gas Pedal",
                "signals": [
                    {"name": "Gas Released", "bix": 4, "len": 1, "unit": "bool"},
                    {"name": "Gas Pedal", "bix": 48, "len": 8, "mul": 0.005, "unit": "fraction"},
                ],
                "source": "built-in",
            },
            0x361: {
                "name": "Throttle",
                "signals": [
                    # raw * 0.343 + 15.8, fitted against OBD PID 0x11
                    {"name": "Throttle Position", "bix": 24, "len": 8, "add": 46.064, "mul": 0.343, "unit": "%"},
                ],
                "source": "built-in",
            },
            0x3D0: {
                "name": "Vehicle Speed 2",
                "signals": [
                    {"name": "Speed", "bix": 0, "len": 8, "unit": "km/h"},
                ],
                "source": "built-in",
            },
            0x498: {
                "name": "Engine Coolant",
                "signals": [
                    {"name": "Coolant Temp", "bix": 24, "len": 8, "mul": 1.25, "unit": "celsius"},
                ],
                "source": "built-in",
            },
            0x49B: {
                "name": "Intake Air",
                "signals": [
                    {"name": "Intake Air Temp", "bix": 56, "len": 8, "mul": 1.25, "unit": "celsius"},
                ],
                "source": "built-in",
            },
        }

        self.obdb_can_ids = {}
        self.signal_definitions = {}

        self.load_obdb()
        self.build_obdb_definitions()
        self.merge_signal_definitions()

    def load_obdb(self):
        """Load OBDb data from JSON file"""
        if not self.obdb_path.exists():
            print(f"⚠️  OBDb file not found: {self.obdb_path}")
            print(f"   Decoder will use built-in definitions only")
            return

        try:
            with open(self.obdb_path, 'r') as f:
                self.obdb_data = json.load(f)
            print(f"✅ Loaded OBDb data: {len(self.obdb_data.get('commands', []))} command definitions")
        except Exception as e:
            print(f"⚠️  Error loading OBDb data: {e}")

    def _build_obdb_message_name(self, cmd):
        """Create a readable name for an OBDb command definition"""
        path = None
        if cmd.get('signals'):
            path = cmd['signals'][0].get('path')

        cmd_bytes = cmd.get('cmd', {})
        cmd_label = None
        if cmd_bytes:
            svc, pid = next(iter(cmd_bytes.items()))
            cmd_label = f"{svc} {pid}"

        if path and cmd_label:
            return f"{path} ({cmd_label})"
        if path:
            return path
        if cmd_label:
            return f"OBDb {cmd_label}"
        return "OBDb Signal"

    def build_obdb_definitions(self):
        """Translate OBDb commands into decoder-friendly signal definitions"""
        if not self.obdb_data:
            return

        for cmd in self.obdb_data.get('commands', []):
            rax = cmd.get('rax')
            if not rax:
                continue

            try:
                can_id = int(str(rax), 16)
            except ValueError:
                continue

            signals = []
            for sig in cmd.get('signals', []):
                fmt = sig.get('fmt', {})
                signals.append({
                    "name": sig.get('name', sig.get('id', 'OBDb Signal')),
                    "bix": fmt.get('bix', 0),
                    "len": fmt.get('len', 8),
                    "add": fmt.get('add'),
                    "mul": fmt.get('mul'),
                    "div": fmt.get('div'),
                    "sign": fmt.get('sign', fmt.get('signed', False)),
                    "unit": fmt.get('unit', 'raw'),
                    "source": "obdb",
                })

            if signals:
                self.obdb_can_ids[can_id] = {
                    "name": self._build_obdb_message_name(cmd),
                    "signals": signals,
                    "source": "obdb",
                }

    def merge_signal_definitions(self):
        """Combine built-in passive decoders with OBDb-derived ones"""
        self.signal_definitions = {
            cid: {
                "name": defn.get('name', 'Unknown'),
                "signals": list(defn.get('signals', [])),
                "source": defn.get('source', 'built-in'),
            }
            for cid, defn in self.manual_can_ids.items()
        }

        for cid, defn in self.obdb_can_ids.items():
            if cid in self.signal_definitions:
                existing = self.signal_definitions[cid]
                merged_signals = existing.get('signals', []) + defn.get('signals', [])
                merged_source = f"{existing.get('source', 'built-in')}+obdb"
                self.signal_definitions[cid] = {
                    "name": existing.get('name') or defn.get('name'),
                    "signals": merged_signals,
                    "source": merged_source,
                }
            else:
                self.signal_definitions[cid] = {
                    "name": defn.get('name', 'Unknown'),
                    "signals": list(defn.get('signals', [])),
                    "source": defn.get('source', 'obdb'),
                }

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
        """Parse a CAN message row from CSV format"""
        try:
            can_id = int(str(row.get('can_id', '')).strip(), 16)
            dlc = int(row.get('dlc', 0))
        except Exception:
            return None

        # Current CSVs name the payload columns b0-b7; the earliest firmware used byte0-byte7
        prefix = 'b' if 'b0' in row else 'byte'
        data_bytes = []
        for i in range(8):
            val = (row.get(f'{prefix}{i}') or '').strip()
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
        """Parse a CANBIN v1 log: 64-byte header, then 24-byte records (docs/BINARY_LOGGING.md)"""
        record_struct = struct.Struct('<QIBB8BH')
        with open(self.log_file, 'rb') as f:
            header = f.read(64)
            if not header.startswith(b'CANBIN'):
                print(f"❌ Error: not a CANBIN log: {self.log_file}")
                return
            while True:
                chunk = f.read(record_struct.size)
                if len(chunk) < record_struct.size:
                    break
                timestamp_us, can_id, dlc, _flags, *data_bytes, _reserved = record_struct.unpack(chunk)
                yield {
                    'id': can_id,
                    'dlc': dlc,
                    'data': data_bytes[:dlc]
                }

    def extract_bits(self, data_bytes, bix, length, signed=False):
        """Extract bits from data bytes

        Args:
            data_bytes: List of bytes
            bix: Bit index (0-based, from MSB of first byte)
            length: Number of bits to extract
            signed: Whether to interpret as signed integer
        """
        # Convert bytes to bit string
        bit_string = ''.join(f'{b:08b}' for b in data_bytes)

        # Extract the bits
        if bix + length > len(bit_string):
            return None

        value_bits = bit_string[bix:bix + length]
        value = int(value_bits, 2)

        # Handle signed values
        if signed and value >= (1 << (length - 1)):
            value -= (1 << length)

        return value

    def decode_signal(self, data_bytes, signal_def):
        """Decode a signal from data bytes using signal definition"""
        bix = signal_def.get('bix', 0)
        length = signal_def.get('len', 8)
        signed = signal_def.get('sign', False)

        # Extract raw value
        raw = self.extract_bits(data_bytes, bix, length, signed)
        if raw is None:
            return None

        # Apply transformations
        value = float(raw)

        add = signal_def.get('add')
        mul = signal_def.get('mul')
        div = signal_def.get('div')

        if add is not None:
            value += float(add)

        if mul is not None:
            value *= float(mul)

        if div is not None:
            value /= float(div)

        return {
            'raw': raw,
            'value': value,
            'unit': signal_def.get('unit', 'unknown'),
            'name': signal_def.get('name', 'Unknown')
        }

    def decode_message(self, can_id, data_bytes):
        """Decode a CAN message"""
        if can_id not in self.signal_definitions:
            return None

        can_def = self.signal_definitions[can_id]
        decoded = {
            'name': can_def['name'],
            'signals': []
        }

        for signal_def in can_def['signals']:
            result = self.decode_signal(data_bytes, signal_def)
            if result:
                decoded['signals'].append(result)

        return decoded

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

                if 'timestamp_us' in first_line.lower():
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

    def print_decoded_summary(self):
        """Print summary of decoded messages"""
        print(f"\n📊 DECODED MESSAGES SUMMARY")
        print("=" * 60)

        decoded_count = 0
        unknown_count = 0

        for can_id in sorted(self.messages_by_id.keys()):
            messages = self.messages_by_id[can_id]
            count = len(messages)

            if can_id in self.signal_definitions:
                can_def = self.signal_definitions[can_id]
                name = can_def['name']
                source = can_def.get('source', 'built-in')
                print(f"✅ 0x{can_id:03X}: {name:<30} ({count:,} messages, {source})")
                decoded_count += count
            else:
                unknown_count += count

        total = decoded_count + unknown_count
        if total > 0:
            pct = (decoded_count / total) * 100
            print(f"\nDecoded: {decoded_count:,}/{total:,} messages ({pct:.1f}%)")

    def print_detailed_decode(self, can_id):
        """Print detailed decode for a specific CAN ID"""
        if can_id not in self.messages_by_id:
            print(f"❌ CAN ID 0x{can_id:03X} not found in log")
            return

        messages = self.messages_by_id[can_id]

        print(f"\n📡 DETAILED DECODE: 0x{can_id:03X}")
        print("=" * 60)
        print(f"Total messages: {len(messages):,}")

        if can_id not in self.signal_definitions:
            print(f"⚠️  No decoder available for this ID")
            print(f"\nFirst 5 messages (raw):")
            for i, msg in enumerate(messages[:5], 1):
                data_str = ' '.join(f"{b:02X}" for b in msg['data'])
                print(f"  {i}. {data_str}")
            return

        can_def = self.signal_definitions[can_id]
        print(f"Name: {can_def['name']}")
        print(f"Source: {can_def.get('source', 'built-in')}")
        print(f"Signals: {len(can_def['signals'])}")

        # Decode first few messages
        print(f"\n📝 Sample Decoded Messages (first 5):")
        for i, msg in enumerate(messages[:5], 1):
            decoded = self.decode_message(can_id, msg['data'])
            data_str = ' '.join(f"{b:02X}" for b in msg['data'])
            print(f"\n  Message {i}: {data_str}")

            if decoded:
                for signal in decoded['signals']:
                    print(f"    {signal['name']:<25}: {signal['value']:>10.2f} {signal['unit']:<10} (raw: 0x{signal['raw']:04X})")

        # Show variation in decoded values
        if len(messages) > 1:
            print(f"\n📈 Value Ranges:")

            # Collect all decoded values
            signal_values = defaultdict(list)
            for msg in messages:
                decoded = self.decode_message(can_id, msg['data'])
                if decoded:
                    for signal in decoded['signals']:
                        signal_values[signal['name']].append(signal['value'])

            # Show ranges
            for signal_name, values in signal_values.items():
                if values:
                    min_val = min(values)
                    max_val = max(values)
                    avg_val = sum(values) / len(values)
                    print(f"  {signal_name:<25}: min={min_val:>8.2f}  max={max_val:>8.2f}  avg={avg_val:>8.2f}")

        print("=" * 60)

    def export_decoded_csv(self, output_file=None):
        """Export decoded data to CSV"""
        if output_file is None:
            output_file = self.log_file.parent / f"{self.log_file.stem}_decoded_obdb.csv"

        with open(output_file, 'w') as f:
            f.write("CAN_ID,Message_Name,Message_Index,Signal_Name,Value,Unit,Raw_Hex\n")

            for can_id in sorted(self.messages_by_id.keys()):
                messages = self.messages_by_id[can_id]
                message_name = self.signal_definitions.get(can_id, {}).get('name', 'Unknown')

                for idx, msg in enumerate(messages):
                    decoded = self.decode_message(can_id, msg['data'])
                    data_str = ' '.join(f"{b:02X}" for b in msg['data'])

                    if decoded:
                        for signal in decoded['signals']:
                            f.write(f"0x{can_id:03X},{message_name},{idx},"
                                  f"{signal['name']},{signal['value']:.2f},{signal['unit']},{data_str}\n")
                    else:
                        f.write(f"0x{can_id:03X},{message_name},{idx},raw,0,unknown,{data_str}\n")

        print(f"\n💾 Decoded data exported to: {output_file}")


def main():
    if len(sys.argv) < 2:
        print("Usage: scripts/decode_with_obdb.py <log_file> [--id 0xXXX] [--export] [--obdb <path>]")
        print("\nExamples:")
        print("  ./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log")
        print("  ./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --id 0x0AA")
        print("  ./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --export")
        print("  ./scripts/decode_with_obdb.py logs/can_capture_20251212_164502.log --obdb ~/custom/path/default.json")
        sys.exit(1)

    log_file = sys.argv[1]

    # Parse options
    specific_id = None
    export = '--export' in sys.argv
    obdb_path = None

    for i, arg in enumerate(sys.argv):
        if arg == '--id' and i + 1 < len(sys.argv):
            try:
                specific_id = int(sys.argv[i + 1], 16)
            except ValueError:
                print(f"❌ Invalid CAN ID: {sys.argv[i + 1]}")
                sys.exit(1)
        elif arg == '--obdb' and i + 1 < len(sys.argv):
            obdb_path = sys.argv[i + 1]

    decoder = OBDbDecoder(log_file, obdb_path)
    if decoder.analyze():
        if specific_id is not None:
            decoder.print_detailed_decode(specific_id)
        else:
            decoder.print_decoded_summary()

        if export:
            decoder.export_decoded_csv()


if __name__ == '__main__':
    main()
