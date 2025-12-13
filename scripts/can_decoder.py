#!/usr/bin/env python3
"""
Toyota 4Runner CAN Bus Decoder & Verification Tool
Use this to test the formulas against your captured data
"""

import re
from collections import defaultdict
from dataclasses import dataclass
from typing import List, Tuple

@dataclass
class TirePressure:
    front_left_psi: float
    front_right_psi: float
    rear_left_psi: float
    rear_right_psi: float

@dataclass
class TireTemp:
    front_left_c: int
    front_right_c: int
    rear_left_c: int
    rear_right_c: int

def decode_tire_pressure_method1(data: List[int]) -> TirePressure:
    """Offset-based formula: (raw - 6400) / 10 = bar"""
    pressures = []
    for i in range(4):
        raw = (data[i*2] << 8) | data[i*2 + 1]
        bar = (raw - 6400.0) / 10.0
        psi = bar * 14.504
        pressures.append(psi)
    
    return TirePressure(*pressures)

def decode_tire_pressure_method2(data: List[int]) -> TirePressure:
    """Direct division formula: raw / 200 = bar"""
    pressures = []
    for i in range(4):
        raw = (data[i*2] << 8) | data[i*2 + 1]
        bar = raw / 200.0
        psi = bar * 14.504
        pressures.append(psi)
    
    return TirePressure(*pressures)

def decode_tire_pressure_method3(data: List[int]) -> TirePressure:
    """Alternative offset formula: (raw - 6500) / 10 = bar"""
    pressures = []
    for i in range(4):
        raw = (data[i*2] << 8) | data[i*2 + 1]
        bar = (raw - 6500.0) / 10.0
        psi = bar * 14.504
        pressures.append(psi)
    
    return TirePressure(*pressures)

def decode_tire_temp(data: List[int]) -> TireTemp:
    """Temperature formula: raw - 40 = Celsius"""
    return TireTemp(
        front_left_c=data[0] - 40,
        front_right_c=data[1] - 40,
        rear_left_c=data[2] - 40,
        rear_right_c=data[3] - 40
    )

def decode_vehicle_speed(data: List[int]) -> float:
    """Speed from 0x024: bytes 4-5, formula: raw / 100"""
    raw_speed = (data[4] << 8) | data[5]
    return raw_speed / 100.0

def decode_steering_angle(data: List[int]) -> float:
    """Steering angle from 0x025: bytes 0-1"""
    raw_angle = (data[0] << 8) | data[1]
    return (raw_angle - 32768) * 0.1

def parse_can_log(filename: str) -> dict:
    """Parse CAN log file and extract messages by ID"""
    messages = defaultdict(list)
    
    can_pattern = re.compile(r'RX ID:0x([0-9A-F]+) DLC:(\d+) Data:([0-9A-F ]+)')
    
    with open(filename, 'r') as f:
        for line in f:
            match = can_pattern.search(line)
            if match:
                can_id = match.group(1)
                dlc = int(match.group(2))
                data_str = match.group(3).strip()
                data = [int(b, 16) for b in data_str.split()]
                
                messages[can_id].append(data)
    
    return messages

def analyze_tire_pressure(messages: List[List[int]]):
    """Test all three tire pressure formulas"""
    print("=" * 80)
    print("TIRE PRESSURE ANALYSIS (CAN ID 0x0AA)")
    print("=" * 80)
    print("\nTesting multiple formulas against your captured data...")
    print("\nExpected: 32-35 PSI for typical 4Runner tire pressure\n")
    
    # Test with first few samples
    for i, data in enumerate(messages[:3]):
        print(f"Sample {i+1}: Raw data = {' '.join(f'{b:02X}' for b in data)}")
        
        # Show raw values
        print(f"  Raw values: ", end="")
        for j in range(4):
            raw = (data[j*2] << 8) | data[j*2 + 1]
            print(f"0x{raw:04X} ({raw}) ", end="")
        print()
        
        # Method 1
        tp1 = decode_tire_pressure_method1(data)
        print(f"  Method 1 (raw-6400)/10*14.504:")
        print(f"    FL:{tp1.front_left_psi:5.1f} FR:{tp1.front_right_psi:5.1f} " +
              f"RL:{tp1.rear_left_psi:5.1f} RR:{tp1.rear_right_psi:5.1f} PSI")
        
        # Method 2
        tp2 = decode_tire_pressure_method2(data)
        print(f"  Method 2 raw/200*14.504:")
        print(f"    FL:{tp2.front_left_psi:5.1f} FR:{tp2.front_right_psi:5.1f} " +
              f"RL:{tp2.rear_left_psi:5.1f} RR:{tp2.rear_right_psi:5.1f} PSI")
        
        # Method 3
        tp3 = decode_tire_pressure_method3(data)
        print(f"  Method 3 (raw-6500)/10*14.504:")
        print(f"    FL:{tp3.front_left_psi:5.1f} FR:{tp3.front_right_psi:5.1f} " +
              f"RL:{tp3.rear_left_psi:5.1f} RR:{tp3.rear_right_psi:5.1f} PSI")
        print()
    
    print("\n📌 CALIBRATION INSTRUCTIONS:")
    print("1. Check your tire pressure with a physical gauge")
    print("2. Note which method matches your actual readings")
    print("3. That's your correct formula!")
    print()

def analyze_tire_temp(messages: List[List[int]]):
    """Analyze tire temperature data"""
    print("=" * 80)
    print("TIRE TEMPERATURE ANALYSIS (CAN ID 0x4A7)")
    print("=" * 80)
    print("\nExpected: ~20-50°C for normal driving conditions\n")
    
    for i, data in enumerate(messages[:3]):
        print(f"Sample {i+1}: Raw data = {' '.join(f'{b:02X}' for b in data)}")
        tt = decode_tire_temp(data)
        print(f"  FL:{tt.front_left_c:3d}°C FR:{tt.front_right_c:3d}°C " +
              f"RL:{tt.rear_left_c:3d}°C RR:{tt.rear_right_c:3d}°C")
        print()

def analyze_speed(messages: List[List[int]]):
    """Analyze vehicle speed"""
    print("=" * 80)
    print("VEHICLE SPEED ANALYSIS (CAN ID 0x024)")
    print("=" * 80)
    print("\nFormula: (bytes[4-5]) / 100 = km/h\n")
    
    unique_speeds = []
    for data in messages:
        speed_kph = decode_vehicle_speed(data)
        if not unique_speeds or abs(speed_kph - unique_speeds[-1]) > 0.1:
            unique_speeds.append(speed_kph)
            if len(unique_speeds) <= 5:
                speed_mph = speed_kph * 0.621371
                print(f"Raw data: {' '.join(f'{b:02X}' for b in data)}")
                print(f"  Speed: {speed_kph:.1f} km/h ({speed_mph:.1f} MPH)")
                print()

def main():
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python3 can_decoder.py <can_log_file>")
        print("\nExample: python3 can_decoder.py can_capture_20251212_164502.log")
        sys.exit(1)
    
    log_file = sys.argv[1]
    print(f"Analyzing CAN log: {log_file}\n")
    
    messages = parse_can_log(log_file)
    
    print(f"Found {len(messages)} unique CAN IDs")
    print(f"Total messages: {sum(len(msgs) for msgs in messages.values())}\n")
    
    # Analyze tire pressure
    if '0AA' in messages:
        analyze_tire_pressure(messages['0AA'])
    else:
        print("⚠️  No tire pressure data (0x0AA) found in log\n")
    
    # Analyze tire temperature
    if '4A7' in messages:
        analyze_tire_temp(messages['4A7'])
    else:
        print("⚠️  No tire temperature data (0x4A7) found in log\n")
    
    # Analyze vehicle speed
    if '024' in messages:
        analyze_speed(messages['024'])
    else:
        print("⚠️  No vehicle speed data (0x024) found in log\n")
    
    # Summary of all IDs
    print("=" * 80)
    print("ALL CAN IDs IN CAPTURE")
    print("=" * 80)
    interesting_ids = {
        '0AA': 'Tire Pressure (TPMS)',
        '4A7': 'Tire Temperature',
        '0B4': 'Wheel Speed',
        '025': 'Steering Angle',
        '024': 'Brake & Speed',
        '1C4': 'Engine/Trans Data',
        '1D0': 'Transmission',
        '3D3': 'Accelerator Position',
        '3B7': '4WD Status',
    }
    
    print(f"\n{'CAN ID':<10} {'Messages':<12} {'Description'}")
    print("-" * 60)
    for can_id in sorted(messages.keys()):
        count = len(messages[can_id])
        desc = interesting_ids.get(can_id, 'Unknown')
        marker = "⭐" if can_id in interesting_ids else "  "
        print(f"{marker} 0x{can_id:<8} {count:<12} {desc}")

if __name__ == '__main__':
    main()
