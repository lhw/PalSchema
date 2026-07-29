#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build_linux"

echo "=== PalSchema Linux Build ==="
echo "Project: ${PROJECT_DIR}"
echo "Build:   ${BUILD_DIR}"

# Check for required tools
for tool in cmake ninja gcc g++; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: $tool not found. Install build dependencies."
        exit 1
    fi
done

# Initialize submodules if needed
if [ ! -f "${PROJECT_DIR}/deps/ue4ss-linux/CMakeLists.txt" ]; then
    echo "=== Initializing submodules ==="
    cd "${PROJECT_DIR}"
    git submodule update --init --recursive
fi

# Build UE4SS Linux first (if not already built)
UE4SS_BUILD_DIR="${PROJECT_DIR}/deps/ue4ss-linux/build_linux"
if [ ! -f "${UE4SS_BUILD_DIR}/lib/libUE4SS.so" ]; then
    echo "=== Building UE4SS Linux ==="
    cd "${PROJECT_DIR}/deps/ue4ss-linux"
    cmake -B build_linux \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Game__Shipping__Linux64 \
        -DUE4SS_GUI_ENABLED=OFF \
        -DUE4SS_INPUT_ENABLED=OFF \
        -DUE4SS_PROFILERS=OFF \
        -Wno-dev
    cmake --build build_linux --target UE4SS --verbose
fi

# Build PalSchema
echo "=== Building PalSchema ==="
cd "${PROJECT_DIR}"
cmake -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUE4SS_DIR="${PROJECT_DIR}/deps/ue4ss-linux" \
    -Wno-dev
cmake --build "${BUILD_DIR}" --verbose

echo "=== Build Complete ==="
echo "Output: ${BUILD_DIR}/libPalSchema.so"
ls -la "${BUILD_DIR}/libPalSchema.so" 2>/dev/null || echo "WARNING: libPalSchema.so not found in build root"
find "${BUILD_DIR}" -name "libPalSchema.so" -type f 2>/dev/null
