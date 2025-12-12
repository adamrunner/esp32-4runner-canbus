#!/bin/bash

# Script to clean CAN log files by removing watchdog errors and backtraces

if [ $# -eq 0 ]; then
    echo "Usage: $0 <log_file> [output_file]"
    echo "  If output_file is not specified, creates a '_clean.log' version"
    echo ""
    echo "Example: $0 logs/can_capture_20251210_162727.log"
    echo "         $0 logs/can_capture_20251210_162727.log logs/clean_output.log"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="${2:-${INPUT_FILE%.log}_clean.log}"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file '$INPUT_FILE' not found"
    exit 1
fi

echo "Sanitizing CAN log file..."
echo "Input:  $INPUT_FILE"
echo "Output: $OUTPUT_FILE"
echo ""

# Count original lines
ORIGINAL_LINES=$(wc -l < "$INPUT_FILE")

# Keep ONLY CAN bus message lines (ID: 0x... DLC: ... Data: ...)
# Remove ANSI color codes first, then grep for CAN messages only
sed -r "s/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[mGK]//g" "$INPUT_FILE" | \
    grep "ID: 0x.*DLC:.*Data:" \
    > "$OUTPUT_FILE"

# Count cleaned lines
CLEANED_LINES=$(wc -l < "$OUTPUT_FILE")
REMOVED_LINES=$((ORIGINAL_LINES - CLEANED_LINES))

echo "Done!"
echo "Original lines: $ORIGINAL_LINES"
echo "Cleaned lines:  $CLEANED_LINES"
echo "Removed lines:  $REMOVED_LINES"
echo ""
echo "Cleaned file saved to: $OUTPUT_FILE"
