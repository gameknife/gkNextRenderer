#!/bin/bash

SETUP_PLATFORM="cmake/SetupPlatform.cmake"

if [ ! -f "$SETUP_PLATFORM" ]; then
    echo "Error: $SETUP_PLATFORM not found."
    exit 1
fi

echo "Checking for Output Directory Standardization in $SETUP_PLATFORM..."

FAILED=0

# Check Debug Postfix
if grep -q "set(CMAKE_DEBUG_POSTFIX d)" "$SETUP_PLATFORM"; then
    echo "PASSED: CMAKE_DEBUG_POSTFIX is set to 'd'"
else
    echo "FAILED: CMAKE_DEBUG_POSTFIX not found or incorrect."
    FAILED=1
fi

# Check Runtime Output Directory (bin)
if grep -q "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \${PROJECT_BINARY_DIR}/bin)" "$SETUP_PLATFORM"; then
    echo "PASSED: CMAKE_RUNTIME_OUTPUT_DIRECTORY is set to 'bin'"
else
    echo "FAILED: CMAKE_RUNTIME_OUTPUT_DIRECTORY is not set to 'bin'."
    FAILED=1
fi

# Check Archive Output Directory (lib)
if grep -q "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY \${PROJECT_BINARY_DIR}/lib)" "$SETUP_PLATFORM"; then
    echo "PASSED: CMAKE_ARCHIVE_OUTPUT_DIRECTORY is set to 'lib'"
else
    echo "FAILED: CMAKE_ARCHIVE_OUTPUT_DIRECTORY is not set to 'lib'."
    FAILED=1
fi

if [ $FAILED -eq 1 ]; then
    echo "Error: Output directory verification failed."
    exit 1
fi

echo "All output directory tests passed!"
exit 0
