#!/bin/bash

# Path to the validator script (to be implemented)
VALIDATOR_SCRIPT="./tools/validate_cmake.sh"

if [ ! -f "$VALIDATOR_SCRIPT" ]; then
    echo "Error: Validator script $VALIDATOR_SCRIPT not found!"
    exit 1
fi

echo "Running tests for CMake Validator..."

# Test 1: Invalid CMakeLists.txt (should fail)
echo "Test 1: Checking invalid CMakeLists.txt (expecting failure)..."
$VALIDATOR_SCRIPT tests/build_system/fixtures/invalid_globals/CMakeLists.txt
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo "FAILED: Validator passed on invalid file."
    exit 1
else
    echo "PASSED: Validator correctly identified issues."
fi

# Test 2: Valid CMakeLists.txt (should pass)
echo "Test 2: Checking valid CMakeLists.txt (expecting success)..."
$VALIDATOR_SCRIPT tests/build_system/fixtures/valid_targets/CMakeLists.txt
EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    echo "FAILED: Validator failed on valid file."
    exit 1
else
    echo "PASSED: Validator accepted valid file."
fi

# Test 3: Missing argument (should fail)
echo "Test 3: Checking missing argument (expecting failure)..."
$VALIDATOR_SCRIPT
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo "FAILED: Validator passed with missing argument."
    exit 1
else
    echo "PASSED: Validator correctly handled missing argument."
fi

# Test 4: File not found (should fail)
echo "Test 4: Checking non-existent file (expecting failure)..."
$VALIDATOR_SCRIPT non_existent_file.txt
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo "FAILED: Validator passed with non-existent file."
    exit 1
else
    echo "PASSED: Validator correctly handled non-existent file."
fi

echo "All tests passed!"
exit 0
