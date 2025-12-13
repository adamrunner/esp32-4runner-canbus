#!/bin/bash
#
# Quick CAN Log Analysis Script
# Runs all validation scripts on a log file for a comprehensive overview
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

if [ $# -eq 0 ]; then
    echo "Usage: $0 <log_file>"
    echo ""
    echo "This script runs a comprehensive analysis on a CAN log file:"
    echo "  1. Validates CAN messages"
    echo "  2. Searches for TPMS data"
    echo "  3. Shows CAN ID summary"
    echo ""
    echo "Example: $0 logs/can_capture_20251212_164502.log"
    exit 1
fi

LOG_FILE="$1"

if [ ! -f "$LOG_FILE" ]; then
    echo "Error: File not found: $LOG_FILE"
    exit 1
fi

echo ""
echo "========================================================================"
echo "  CAN LOG COMPREHENSIVE ANALYSIS"
echo "  Log file: $LOG_FILE"
echo "========================================================================"
echo ""

# Step 1: Validation
echo -e "${BLUE}[1/3] VALIDATING CAN MESSAGES${NC}"
echo "------------------------------------------------------------------------"
python3 "$SCRIPT_DIR/validate_can.py" "$LOG_FILE"

echo ""
echo ""

# Step 2: TPMS Search
echo -e "${BLUE}[2/3] SEARCHING FOR TPMS DATA${NC}"
echo "------------------------------------------------------------------------"
python3 "$SCRIPT_DIR/find_tpms.py" "$LOG_FILE"

echo ""
echo ""

# Step 3: CAN ID Summary
echo -e "${BLUE}[3/3] CAN ID SUMMARY${NC}"
echo "------------------------------------------------------------------------"
python3 "$SCRIPT_DIR/decode_can.py" "$LOG_FILE"

echo ""
echo ""
echo "========================================================================"
echo -e "${GREEN}✓ ANALYSIS COMPLETE${NC}"
echo "========================================================================"
echo ""
echo "Next steps:"
echo "  • To analyze a specific CAN ID: ./scripts/decode_can.py $LOG_FILE --id 0xXXX"
echo "  • To export TPMS data: ./scripts/find_tpms.py $LOG_FILE --export"
echo "  • To export all decoded data: ./scripts/decode_can.py $LOG_FILE --export"
echo ""
