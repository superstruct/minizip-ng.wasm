#!/bin/bash
# minizip-ng.wasm Production Build Script
# Copyright (C) 2025 Superstruct Ltd, New Zealand
# Licensed under the same terms as minizip-ng

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_DIR="${SCRIPT_DIR}/install"
BUILD_TYPE="${BUILD_TYPE:-Release}"
ENABLE_SIMD="${ENABLE_SIMD:-ON}"
ENABLE_TESTS="${ENABLE_TESTS:-ON}"

# Ecosystem integration paths
ECOSYSTEM_ROOT="${ECOSYSTEM_ROOT:-../..}"
ECOSYSTEM_ZLIB_DIR="${ECOSYSTEM_ZLIB_DIR:-${ECOSYSTEM_ROOT}/zlib.wasm/install}"
ECOSYSTEM_BZIP2_DIR="${ECOSYSTEM_BZIP2_DIR:-${ECOSYSTEM_ROOT}/bzip2.wasm/install}" 
ECOSYSTEM_ZSTD_DIR="${ECOSYSTEM_ZSTD_DIR:-${ECOSYSTEM_ROOT}/zstd.wasm/install}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[minizip-ng.wasm]${NC} $*"
}

warn() {
    echo -e "${YELLOW}[minizip-ng.wasm]${NC} $*"
}

error() {
    echo -e "${RED}[minizip-ng.wasm]${NC} $*" >&2
}

# Clean build directory if requested
if [[ "${1:-}" == "clean" ]]; then
    log "Cleaning build directory..."
    rm -rf "${BUILD_DIR}" "${INSTALL_DIR}"
    exit 0
fi

# Verify Emscripten is available
if ! command -v emcc >/dev/null 2>&1; then
    error "Emscripten not found. Please install and activate Emscripten SDK."
    exit 1
fi

# Display build configuration
log "Starting minizip-ng.wasm build"
log "Build type: ${BUILD_TYPE}"
log "SIMD enabled: ${ENABLE_SIMD}"
log "Tests enabled: ${ENABLE_TESTS}"
log "Build directory: ${BUILD_DIR}"
log "Install directory: ${INSTALL_DIR}"

# Check for ecosystem dependencies
check_dependency() {
    local name="$1"
    local dir="$2"
    
    if [[ -d "${dir}" ]]; then
        log "Found ecosystem dependency: ${name} at ${dir}"
        return 0
    else
        warn "Ecosystem dependency not found: ${name} at ${dir}"
        return 1
    fi
}

# Verify ecosystem dependencies
ZLIB_AVAILABLE=0
BZIP2_AVAILABLE=0
ZSTD_AVAILABLE=0

if check_dependency "zlib.wasm" "${ECOSYSTEM_ZLIB_DIR}"; then
    ZLIB_AVAILABLE=1
fi

if check_dependency "bzip2.wasm" "${ECOSYSTEM_BZIP2_DIR}"; then
    BZIP2_AVAILABLE=1
fi

if check_dependency "zstd.wasm" "${ECOSYSTEM_ZSTD_DIR}"; then
    ZSTD_AVAILABLE=1
fi

# Create build directory
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}"

# Create test data directory
mkdir -p "${SCRIPT_DIR}/test/data"

# Generate test ZIP files if they don't exist
if [[ ! -f "${SCRIPT_DIR}/test/data/sample.zip" ]]; then
    log "Generating test data..."
    cd "${SCRIPT_DIR}/test/data"
    
    # Create sample files
    echo "Hello from minizip-ng.wasm!" > hello.txt
    echo "This is a test file for ZIP operations." > test.txt
    mkdir -p subdir
    echo "File in subdirectory" > subdir/nested.txt
    
    # Create a simple ZIP file for testing (using system zip if available)
    if command -v zip >/dev/null 2>&1; then
        zip -r sample.zip hello.txt test.txt subdir/
        log "Test ZIP file created: test/data/sample.zip"
    else
        warn "System 'zip' command not found, some tests may fail"
    fi
    
    cd "${SCRIPT_DIR}"
fi

# Configure CMake build
log "Configuring CMake build..."
cd "${BUILD_DIR}"

# Base CMake arguments
CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"
    "-DCMAKE_TOOLCHAIN_FILE=${EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DMZ_BUILD_TESTS=${ENABLE_TESTS}"
    "-DMZ_BUILD_UNIT_TESTS=${ENABLE_TESTS}"
    "-DMZ_COMPAT=ON"
    "-DMZ_FETCH_LIBS=OFF"
)

# WASM-specific features
CMAKE_ARGS+=(
    "-DMZ_WASM=ON"
    "-DMZ_WASM_NATIVE=ON"
    "-DMZ_WASM_SIMD=${ENABLE_SIMD}"
)

# Compression library configuration
if [[ "${ZLIB_AVAILABLE}" == "1" ]]; then
    CMAKE_ARGS+=(
        "-DMZ_ZLIB=ON"
        "-DECOSYSTEM_ZLIB_DIR=${ECOSYSTEM_ZLIB_DIR}"
    )
else
    CMAKE_ARGS+=("-DMZ_ZLIB=ON") # Use system/bundled zlib
fi

if [[ "${BZIP2_AVAILABLE}" == "1" ]]; then
    CMAKE_ARGS+=(
        "-DMZ_BZIP2=ON"
        "-DECOSYSTEM_BZIP2_DIR=${ECOSYSTEM_BZIP2_DIR}"
    )
else
    CMAKE_ARGS+=("-DMZ_BZIP2=OFF")
fi

if [[ "${ZSTD_AVAILABLE}" == "1" ]]; then
    CMAKE_ARGS+=(
        "-DMZ_ZSTD=ON"
        "-DECOSYSTEM_ZSTD_DIR=${ECOSYSTEM_ZSTD_DIR}"
    )
else
    CMAKE_ARGS+=("-DMZ_ZSTD=OFF")
fi

# Disable complex features for initial implementation
CMAKE_ARGS+=(
    "-DMZ_LZMA=OFF"
    "-DMZ_LIBCOMP=OFF"
    "-DMZ_OPENSSL=OFF"
    "-DMZ_ICONV=OFF"
)

# WASM compiler flags
export CFLAGS="-O3 -msimd128 -DMINIZIP_WASM=1"
export CXXFLAGS="-O3 -msimd128 -DMINIZIP_WASM=1"

# WASM linker flags
WASM_LINK_FLAGS=(
    "-s WASM=1"
    "-s MODULARIZE=1"
    "-s EXPORT_ES6=1"
    "-s USE_ES6_IMPORT_META=0"
    "-s ENVIRONMENT=web,worker,node"
    "-s EXPORT_NAME=MinizipNGModule"
    "-s INITIAL_MEMORY=64MB"
    "-s MAXIMUM_MEMORY=512MB"
    "-s ALLOW_MEMORY_GROWTH=1"
    "-s FORCE_FILESYSTEM=1"
    "-lidbfs.js"
    "-s EXPORTED_FUNCTIONS=['_malloc','_free']"
    "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']"
)

if [[ "${ENABLE_SIMD}" == "ON" ]]; then
    WASM_LINK_FLAGS+=("-msimd128")
fi

if [[ "${BUILD_TYPE}" == "Release" ]]; then
    WASM_LINK_FLAGS+=(
        "-O3"
        "--closure=1"
        "-flto"
    )
else
    WASM_LINK_FLAGS+=(
        "-O1"
        "-g"
        "-s ASSERTIONS=1"
        "-s SAFE_HEAP=1"
    )
fi

export LDFLAGS="${WASM_LINK_FLAGS[*]}"

# Run CMake configuration
log "Running CMake configuration..."
cmake "${CMAKE_ARGS[@]}" -f "${SCRIPT_DIR}/CMakeLists.wasm.txt" "${SCRIPT_DIR}"

# Build the project
log "Building minizip-ng.wasm..."
make -j"$(nproc)" VERBOSE=1

# Install files
log "Installing minizip-ng.wasm..."
make install

# Validate build artifacts
log "Validating build artifacts..."
WASM_FILE=""
JS_FILE=""

# Find generated WASM and JS files
for file in "${BUILD_DIR}"/*.wasm; do
    if [[ -f "$file" ]]; then
        WASM_FILE="$file"
        break
    fi
done

for file in "${BUILD_DIR}"/*.js; do
    if [[ -f "$file" ]]; then
        JS_FILE="$file"
        break
    fi
done

if [[ -n "$WASM_FILE" && -f "$WASM_FILE" ]]; then
    WASM_SIZE=$(stat -c%s "$WASM_FILE")
    log "WASM module built: $(basename "$WASM_FILE") (${WASM_SIZE} bytes)"
    
    # Copy to install directory
    cp "$WASM_FILE" "${INSTALL_DIR}/"
else
    error "WASM module not found!"
    exit 1
fi

if [[ -n "$JS_FILE" && -f "$JS_FILE" ]]; then
    JS_SIZE=$(stat -c%s "$JS_FILE")
    log "JS module built: $(basename "$JS_FILE") (${JS_SIZE} bytes)"
    
    # Copy to install directory
    cp "$JS_FILE" "${INSTALL_DIR}/"
else
    warn "JS module not found, this may be expected depending on build configuration"
fi

# Copy WASM-native JavaScript wrapper
if [[ -f "${SCRIPT_DIR}/wasm/minizip-ng-wasm.js" ]]; then
    cp "${SCRIPT_DIR}/wasm/minizip-ng-wasm.js" "${INSTALL_DIR}/"
    log "WASM-native JavaScript wrapper copied"
fi

# Run basic validation tests
if [[ "${ENABLE_TESTS}" == "ON" ]]; then
    log "Running validation tests..."
    
    # Check if test executables were built
    if [[ -f "${BUILD_DIR}/test/minizip-ng-test" ]]; then
        log "Running unit tests..."
        "${BUILD_DIR}/test/minizip-ng-test"
    else
        warn "Unit tests not built (this may be expected for WASM builds)"
    fi
fi

# Display build summary
log "Build completed successfully!"
log ""
log "Build Summary:"
log "=============="
log "Build type: ${BUILD_TYPE}"
log "SIMD: ${ENABLE_SIMD}"
log "WASM file: $(basename "${WASM_FILE:-'N/A'}") ($(numfmt --to=iec "${WASM_SIZE:-0}"))"
log "JS file: $(basename "${JS_FILE:-'N/A'}") ($(numfmt --to=iec "${JS_SIZE:-0}" 2>/dev/null || echo "N/A"))"
log "Install directory: ${INSTALL_DIR}"
log ""
log "Enabled compression formats:"
[[ "${ZLIB_AVAILABLE}" == "1" ]] && log "  ✓ ZLIB (ecosystem)" || log "  ✓ ZLIB (bundled)"
[[ "${BZIP2_AVAILABLE}" == "1" ]] && log "  ✓ BZIP2 (ecosystem)" || log "  ✗ BZIP2"
[[ "${ZSTD_AVAILABLE}" == "1" ]] && log "  ✓ ZSTD (ecosystem)" || log "  ✗ ZSTD"
log "  ✗ LZMA (disabled)"
log ""
log "Next steps:"
log "  - Run 'npm run test' to run comprehensive tests"
log "  - Run 'npm run benchmark' to run performance tests"
log "  - Use 'act -j build-wasm' to test GitHub Actions locally"