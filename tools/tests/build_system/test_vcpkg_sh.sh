#!/bin/bash

# ==============================================================================
# Test script for vcpkg.sh CLI compliance
# ==============================================================================

VCPKG_SH="./scripts/vcpkg.sh"

if [ ! -f "$VCPKG_SH" ]; then
    echo "Error: vcpkg.sh not found!"
    exit 1
fi

# Mock environment
TMP_DIR=$(mktemp -d)

# Copy vcpkg.sh to temp dir
# We need to mock .vcpkg directory logic or allow it to try (and fail safely or mock git)
# Since the script clones git, we definitely want to mock things.
# But for CLI argument parsing, we can just ensure it doesn't error out on args.
# However, the script tries to run logic immediately.

# We will modify the copy of vcpkg.sh to mock the helper functions
# This is a bit intrusive test but necessary for shell scripts that do heavy lifting.

cp "$VCPKG_SH" "$TMP_DIR/vcpkg.sh"
chmod +x "$TMP_DIR/vcpkg.sh"

# In the test copy, replace main logic with argument dumping
# We want to verify `parse_arguments` logic mostly.
# But the script calls main "$@" at the end.

# Let's mock the `ensure_repo` and `ensure_bootstrap` functions in the script
# by appending redefinitions to the end (before the main call if possible, or just overriding)
# Bash function override works if defined after.
# But the script calls main at the very end.

# Strategy: Append mocks to the file before execution.
# But wait, the script executes `main "$@"` as the last line.
# If I append to it, I'm appending after the call.
# I need to prepend? No.
# I will use `sed` to replace the function calls in `main` or replace the functions themselves.

# Actually, I can just source the script in a subshell with mocked functions IF the script uses a guard.
# It doesn't use a guard. It calls main at the end.
# So I will use sed to comment out the `main "$@"` call at the end, source it, mock functions, and then call main.

sed -i.bak 's/^main "$@"/# main "$@"/' "$TMP_DIR/vcpkg.sh"

cat >> "$TMP_DIR/test_wrapper.sh" <<EOF
#!/bin/bash
source "$TMP_DIR/vcpkg.sh"

# Mock functions
ensure_repo() { echo "MOCK: ensure_repo"; }
ensure_bootstrap() { echo "MOCK: ensure_bootstrap"; }
# init_variables might need mocking if it checks dirs?
# It checks VCPKG_DEFAULT_BINARY_CACHE and mkdirs it. That's fine in TMP.
# It sets PROJECT_ROOT based on SCRIPT_DIR.

# Run main
main "\$@"
EOF
chmod +x "$TMP_DIR/test_wrapper.sh"

PASSED_TESTS=0
FAILED_TESTS=0
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

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

echo "Running vcpkg.sh CLI compliance tests..."

# 1. Help
assert_output_contains "$TMP_DIR/test_wrapper.sh --help" "Usage: ./vcpkg.sh" "Display help"

# 2. No args (should succeed now, previously failed)
assert_output_contains "$TMP_DIR/test_wrapper.sh" "MOCK: ensure_repo" "Run without args"

# 3. Update flag
assert_output_contains "$TMP_DIR/test_wrapper.sh --update" "MOCK: ensure_repo" "Run with --update"

rm -rf "$TMP_DIR"

echo "---------------------------------------"
echo "Tests Passed: $PASSED_TESTS"
echo "Tests Failed: $FAILED_TESTS"

if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
fi
exit 0
