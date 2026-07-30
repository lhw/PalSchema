#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build_linux"

echo "=== PalSchema Linux Build ==="
echo "Project: ${PROJECT_DIR}"
echo "Build:   ${BUILD_DIR}"

# Parse arguments
USE_DOCKER=false
DOCKER_PLATFORM="linux/amd64"
for arg in "$@"; do
    case "$arg" in
        --docker|--container)
            USE_DOCKER=true
            ;;
        --platform=*)
            DOCKER_PLATFORM="${arg#*=}"
            ;;
    esac
done

if [ "$USE_DOCKER" = true ]; then
    echo "=== Building via Docker/Apple Containers ==="
    echo "Platform: ${DOCKER_PLATFORM}"
    
    # Try Apple Containers first, fall back to Docker
    if command -v container &>/dev/null; then
        echo "Using Apple Containers (container build)"
        container build \
            --platform "${DOCKER_PLATFORM}" \
            --progress=plain \
            --tag palschema-linux \
            "${PROJECT_DIR}"
        echo "=== Docker Build Complete ==="
        echo "Image: palschema-linux"
        echo "To extract: container create --name palschema-output palschema-linux"
        echo "            container cp palschema-output:/output/libPalSchema.so ./"
    elif command -v docker &>/dev/null; then
        echo "Using Docker"
        docker build \
            --platform "${DOCKER_PLATFORM}" \
            --progress=plain \
            --tag palschema-linux \
            "${PROJECT_DIR}"
        echo "=== Docker Build Complete ==="
        echo "Image: palschema-linux"
        echo "To extract: docker create --name palschema-output palschema-linux"
        echo "            docker cp palschema-output:/output/libPalSchema.so ./"
    else
        echo "ERROR: Neither 'container' (Apple Containers) nor 'docker' found."
        echo "Install Docker Desktop or Apple Containers, or build natively."
        exit 1
    fi
    exit 0
fi

# Native build (requires Linux x86-64 toolchain)
echo "=== Building natively ==="

# Check for required tools
for tool in cmake ninja gcc g++; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: $tool not found. Install build dependencies."
        echo "       Or use: $0 --docker for containerized build."
        exit 1
    fi
done

# Check architecture
ARCH=$(uname -m)
if [ "$ARCH" != "x86_64" ]; then
    echo "WARNING: Building on $ARCH but PalServer is x86_64."
    echo "         The .so may not work on the target system."
    echo "         Consider using: $0 --docker for cross-compilation."
fi

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
