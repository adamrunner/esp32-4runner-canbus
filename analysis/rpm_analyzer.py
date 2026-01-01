#!/usr/bin/env python3
"""
RPM Analyzer - Analyze CAN logs for engine RPM candidates
"""
import pandas as pd
import numpy as np
import sys

def analyze_rpm_candidate(csv_file, can_id):
    """Analyze a specific CAN ID as an RPM candidate"""
    print(f"\n{'='*80}")
    print(f"Analyzing CAN ID 0x{can_id.upper()} for RPM patterns")
    print(f"{'='*80}")
    
    df = pd.read_csv(csv_file)
    df_filtered = df[df['can_id'].str.lower() == can_id.lower()].copy()
    
    if len(df_filtered) == 0:
        print(f"No messages found for CAN ID 0x{can_id.upper()}")
        return
    
    print(f"\nTotal messages: {len(df_filtered)}")
    print(f"Duration: {(df_filtered['timestamp_us'].max() - df_filtered['timestamp_us'].min()) / 1e6:.2f} seconds")
    print(f"Message rate: {len(df_filtered) / ((df_filtered['timestamp_us'].max() - df_filtered['timestamp_us'].min()) / 1e6):.1f} Hz")
    
    # Extract byte columns
    byte_cols = [f'b{i}' for i in range(8)]
    for col in byte_cols:
        df_filtered[f'{col}_int'] = df_filtered[col].apply(lambda x: int(x, 16) if isinstance(x, str) else x)
    
    print(f"\n{'='*80}")
    print("Statistics for each byte (as integers):")
    print(f"{'='*80}")
    for i in range(8):
        col = f'b{i}_int'
        stats = df_filtered[col].describe()
        unique_count = df_filtered[col].nunique()
        print(f"Byte {i}: min={stats['min']:.0f}, max={stats['max']:.0f}, mean={stats['mean']:.2f}, "
              f"std={stats['std']:.2f}, unique={unique_count}")
    
    # Analyze each byte's variation over time
    print(f"\n{'='*80}")
    print("Temporal variation analysis:")
    print(f"{'='*80}")
    
    # Split data into 3 segments (beginning, middle, end)
    total_rows = len(df_filtered)
    segments = [
        ('First 10%', df_filtered.iloc[:int(total_rows*0.1)]),
        ('First third', df_filtered.iloc[:int(total_rows/3)]),
        ('Middle third', df_filtered.iloc[int(total_rows/3):int(2*total_rows/3)]),
        ('Last third', df_filtered.iloc[int(2*total_rows/3):]),
        ('Last 10%', df_filtered.iloc[-int(total_rows*0.1):])
    ]
    
    for seg_name, seg_df in segments:
        if len(seg_df) > 0:
            print(f"\n{seg_name} ({len(seg_df)} messages):")
            for i in range(8):
                col = f'b{i}_int'
                mean_val = seg_df[col].mean()
                min_val = seg_df[col].min()
                max_val = seg_df[col].max()
                print(f"  Byte {i}: mean={mean_val:.2f}, range=[{min_val:.0f}, {max_val:.0f}]")
    
    # Show sample data at different time points
    print(f"\n{'='*80}")
    print("Sample data at different time points:")
    print(f"{'='*80}")
    
    sample_points = [0, int(len(df_filtered)*0.25), int(len(df_filtered)*0.5), 
                     int(len(df_filtered)*0.75), len(df_filtered)-1]
    
    for idx in sample_points:
        row = df_filtered.iloc[idx]
        timestamp_s = row['timestamp_us'] / 1e6
        hex_bytes = []
        for i in range(8):
            byte_val = row[f'b{i}']
            if isinstance(byte_val, str):
                hex_bytes.append(f"{int(byte_val, 16):02X}")
            else:
                hex_bytes.append(f"{int(byte_val):02X}")
        hex_data = ' '.join(hex_bytes)
        print(f"t={timestamp_s:.1f}s: {hex_data}")
    
    # Test common RPM encodings
    print(f"\n{'='*80}")
    print("Testing common RPM encodings:")
    print(f"{'='*80}")
    
    encodings = []
    
    # Test 8-bit bytes directly
    for i in range(8):
        col = f'b{i}_int'
        col_data = df_filtered[col]
        if col_data.nunique() > 1:
            encodings.append({
                'name': f'Byte {i} (8-bit)',
                'values': col_data.values,
                'min': col_data.min(),
                'max': col_data.max(),
                'range': col_data.max() - col_data.min()
            })
    
    # Test 16-bit big-endian combinations
    for i in range(7):
        col1 = f'b{i}_int'
        col2 = f'b{i+1}_int'
        values = (df_filtered[col1] * 256 + df_filtered[col2]).values
        unique_vals = np.unique(values)
        if len(unique_vals) > 1:
            encodings.append({
                'name': f'Bytes {i}-{i+1} (16-bit BE)',
                'values': values,
                'min': values.min(),
                'max': values.max(),
                'range': values.max() - values.min()
            })
    
    # Test 16-bit little-endian combinations
    for i in range(7):
        col1 = f'b{i+1}_int'
        col2 = f'b{i}_int'
        values = (df_filtered[col1] * 256 + df_filtered[col2]).values
        unique_vals = np.unique(values)
        if len(unique_vals) > 1:
            encodings.append({
                'name': f'Bytes {i+1}-{i} (16-bit LE)',
                'values': values,
                'min': values.min(),
                'max': values.max(),
                'range': values.max() - values.min()
            })
    
    # Test common scaling factors
    scaling_factors = [1, 0.25, 0.5, 0.125, 4, 8, 12.5, 25, 32, 64, 100]
    
    print(f"\n{'Encoding':<30} {'Raw Range':<15} {'Scaled Range (RPM)':<20} {'Best Scaling'}")
    print("-" * 85)
    
    for enc in encodings:
        raw_min, raw_max = enc['min'], enc['max']
        raw_range = enc['range']
        
        best_scale = None
        best_rpm_range = (0, 0)
        
        for scale in scaling_factors:
            rpm_min = raw_min * scale
            rpm_max = raw_max * scale
            rpm_range = rpm_max - rpm_min
            
            # Check if this could represent RPM (0-8000 range)
            if 0 <= rpm_min <= 1000 and 0 <= rpm_max <= 10000:
                if best_scale is None or abs(rpm_range - 1700) < abs(best_rpm_range[1] - best_rpm_range[0] - 1700):
                    best_scale = scale
                    best_rpm_range = (rpm_min, rpm_max)
        
        if best_scale:
            print(f"{enc['name']:<30} [{raw_min:.0f}, {raw_max:.0f}]     "
                  f"[{best_rpm_range[0]:.0f}, {best_rpm_range[1]:.0f}]         x{best_scale}")
        else:
            print(f"{enc['name']:<30} [{raw_min:.0f}, {raw_max:.0f}]     N/A                         N/A")
    
    # Show the first and last values for the best candidates
    print(f"\n{'='*80}")
    print("First vs Last values comparison (for top candidates):")
    print(f"{'='*80}")
    
    for enc in encodings[:5]:  # Show top 5 candidates
        values = enc['values']
        first_val = values[0]
        last_val = values[-1]
        min_val = values.min()
        max_val = values.max()
        
        print(f"\n{enc['name']}:")
        print(f"  First: {first_val:.0f}, Last: {last_val:.0f}")
        print(f"  Min: {min_val:.0f}, Max: {max_val:.0f}")
        print(f"  Change: {last_val - first_val:.0f} ({(last_val - first_val) / (first_val + 1) * 100:.1f}%)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 rpm_analyzer.py <csv_file> <can_id>")
        print("\nExample:")
        print("  python3 rpm_analyzer.py logs/CAN_0003.CSV 2C1")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    can_id = sys.argv[2]
    analyze_rpm_candidate(csv_file, can_id)
