#!/bin/bash

# Batch clean all log files in the logs directory

LOGS_DIR="logs"

if [ ! -d "$LOGS_DIR" ]; then
    echo "Error: logs directory not found"
    exit 1
fi

echo "Cleaning all log files in $LOGS_DIR/"
echo "========================================"
echo ""

# Count total files
TOTAL_FILES=$(find "$LOGS_DIR" -name "*.log" ! -name "*_clean.log" | wc -l)

if [ "$TOTAL_FILES" -eq 0 ]; then
    echo "No log files found to clean"
    exit 0
fi

echo "Found $TOTAL_FILES log file(s) to clean"
echo ""

# Process each log file
find "$LOGS_DIR" -name "*.log" ! -name "*_clean.log" | while read -r logfile; do
    echo "Processing: $logfile"
    ./sanitize_logs.sh "$logfile"
    echo ""
done

echo "========================================"
echo "All log files cleaned!"
echo ""
echo "Original files: logs/*_<timestamp>.log"
echo "Cleaned files:  logs/*_<timestamp>_clean.log"
