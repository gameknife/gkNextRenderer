#!/bin/bash

# ==============================================================================
# Test script for run.sh CLI compliance
# ==============================================================================

RUN_SH="./run.sh"

if [ ! -f "$RUN_SH" ]; then
    echo "Error: run.sh not found!"
    exit 1
fi

# Mock environment
TMP_DIR=$(mktemp -d)

# Create a mock bin directory structure matching the expected layout
# out/build/test-preset/bin/gkNextRenderer
mkdir -p "$TMP_DIR/out/build/test-preset/bin"
touch "$TMP_DIR/out/build/test-preset/bin/gkNextRenderer"
chmod +x "$TMP_DIR/out/build/test-preset/bin/gkNextRenderer"
touch "$TMP_DIR/out/build/test-preset/bin/OtherApp"
chmod +x "$TMP_DIR/out/build/test-preset/bin/OtherApp"

# Copy run.sh to temp dir to modify its script_dir resolution behavior?
# No, run.sh uses `dirname "${BASH_SOURCE[0]}"`.
# If I run it from root, it expects out/build relative to root.
# So I need to mock the out directory in the PROJECT ROOT or override script_dir?
# run.sh calculates script_dir.
# I can rely on `--bin-dir` to point to my temp dir for some tests,
# but for `--preset` tests I need the directory to exist relative to run.sh.
#
# Workaround: Create a temporary symlink "out" in the current directory if it doesn't exist?
# No, that's dangerous.
#
# Better: Copy run.sh to the temp directory and run it there.
cp "$RUN_SH" "$TMP_DIR/run.sh"
chmod +x "$TMP_DIR/run.sh"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASSED_TESTS=0
FAILED_TESTS=0

assert_output_contains() {
    local cmd="$1"
    local pattern="$2"
    local desc="$3"
    echo -n "Test: $desc... "
    local output
    # Execute inside TMP_DIR so it finds local 'out' folder
    output=$(cd "$TMP_DIR" && eval "$cmd" 2>&1)
    
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

echo "Running run.sh CLI compliance tests..."

# 1. Help
assert_output_contains "./run.sh --help" "Usage: ./run.sh" "Display help"

# 2. List with preset
assert_output_contains "./run.sh --preset test-preset --list" "gkNextRenderer" "List binaries in preset"

# 3. Dry run with target (space separated)
assert_output_contains "./run.sh --preset test-preset --target OtherApp --dry-run" "OtherApp" "Dry run target (space)"

# 4. Dry run with target (equals separated)
assert_output_contains "./run.sh --preset=test-preset --target=OtherApp --dry-run" "OtherApp" "Dry run target (equals)"

# 5. App arguments
assert_output_contains "./run.sh --preset test-preset --dry-run -- --foo bar" "--foo bar" "Pass app arguments"

# Cleanup
rm -rf "$TMP_DIR"

echo "---------------------------------------"
echo "Tests Passed: $PASSED_TESTS"
echo "Tests Failed: $FAILED_TESTS"

if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
fi
exit 0
