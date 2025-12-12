#!/bin/bash

# Create logs directory if it doesn't exist
mkdir -p logs

# Generate timestamp for filename
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="logs/can_capture_${TIMESTAMP}.log"

echo "Starting CAN bus capture..."
echo "Output will be saved to: $LOG_FILE"
echo "Press Ctrl+] to exit monitor"
echo ""

# Run idf.py monitor and tee output to both terminal and log file
idf.py monitor | tee "$LOG_FILE"

echo ""
echo "Capture saved to: $LOG_FILE"
