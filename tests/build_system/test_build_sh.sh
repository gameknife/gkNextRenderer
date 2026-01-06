#!/bin/bash

# ==============================================================================
# Test script for build.sh CLI compliance
# ==============================================================================

BUILD_SH="./build.sh"

if [ ! -f "$BUILD_SH" ]; then
    echo "Error: build.sh not found!"
    exit 1
fi

# Use a dummy cmake to avoid actual building during CLI tests
# We'll put it in a temp directory and add to PATH
TMP_DIR=$(mktemp -d)
cat > "$TMP_DIR/cmake" <<EOF
#!/bin/bash
# Mock cmake that just logs arguments
echo "MOCK_CMAKE_ARGS: \$*"
exit 0
EOF
chmod +x "$TMP_DIR/cmake"

# Mock tools that might be called
mkdir -p "$TMP_DIR/tools/tsc"
touch "$TMP_DIR/tools/tsc/tsc"
chmod +x "$TMP_DIR/tools/tsc/tsc"

ORIGINAL_PATH="$PATH"
export PATH="$TMP_DIR:$PATH"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASSED_TESTS=0
FAILED_TESTS=0

assert_success() {
    local cmd="$1"
    local desc="$2"
    echo -n "Test: $desc... "
    if eval "$cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}FAILED${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

assert_failure() {
    local cmd="$1"
    local desc="$2"
    echo -n "Test: $desc... "
    if ! eval "$cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}FAILED${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

assert_output_contains() {
    local cmd="$1"
    local pattern="$2"
    local desc="$3"
    echo -n "Test: $desc... "
    local output
    output=$(eval "$cmd" 2>&1)
    if echo "$output" | grep -Fq -- "$pattern"; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}FAILED${NC}"
        echo "  Expected pattern: $pattern"
        echo "  Actual output: $output"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

echo "Running build.sh CLI compliance tests..."

# 1. Help
assert_success "$BUILD_SH --help" "Display help with --help"
assert_success "$BUILD_SH -h" "Display help with -h"

# 2. Preset (New spec: --preset <name>)
assert_output_contains "$BUILD_SH --preset my-preset" "Configuring preset: my-preset" "Set preset via --preset"

# 3. Config (New spec: --config <type>)
# Note: For multi-config generators, this might be passed to build, 
# for single-config it might be a variable. We check if it's handled.
assert_output_contains "$BUILD_SH --config Release" "Release" "Set config via --config"

# 4. Target (New spec: --target <name>)
assert_output_contains "$BUILD_SH --target my-app" "--target my-app" "Set build target via --target"

# 5. Clean
assert_output_contains "$BUILD_SH --clean" "Cleaning build" "Handle --clean"

# Cleanup
rm -rf "$TMP_DIR"
export PATH="$ORIGINAL_PATH"

echo "---------------------------------------"
echo "Tests Passed: $PASSED_TESTS"
echo "Tests Failed: $FAILED_TESTS"

if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
fi
exit 0
