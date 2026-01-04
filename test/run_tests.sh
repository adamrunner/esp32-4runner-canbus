#!/bin/bash
#
# Build and run host-based unit tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=== Building unit tests ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake ..
make

echo ""
echo "=== Running unit tests ==="
./test_can_signal

echo ""
echo "=== All tests passed ==="
