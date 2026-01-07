#!/bin/bash

CMAKELISTS="CMakeLists.txt"
MODULES_DIR="cmake"

if [ ! -f "$CMAKELISTS" ]; then
    echo "Error: $CMAKELISTS not found."
    exit 1
fi

EXPECTED_MODULES=(
    "SetupPlatform.cmake"
    "ProjectOptions.cmake"
    "SetupDependencies.cmake"
)

MISSING_MODULES=0

echo "Checking for expected CMake modules..."
for module in "${EXPECTED_MODULES[@]}"; do
    if [ ! -f "$MODULES_DIR/$module" ]; then
        echo "FAILED: Missing module $MODULES_DIR/$module"
        MISSING_MODULES=1
    else
        echo "PASSED: Found $MODULES_DIR/$module"
    fi
done

if [ $MISSING_MODULES -eq 1 ]; then
    echo "Error: Some required modules are missing."
    exit 1
fi

echo "Checking CMakeLists.txt for module inclusions..."
MISSING_INCLUDES=0

for module in "${EXPECTED_MODULES[@]}"; do
    # Check if the module is included (naive check for filename)
    if ! grep -q "$module" "$CMAKELISTS"; then
        echo "FAILED: $CMAKELISTS does not include $module"
        MISSING_INCLUDES=1
    else
        echo "PASSED: $CMAKELISTS includes $module"
    fi
done

if [ $MISSING_INCLUDES -eq 1 ]; then
    echo "Error: CMakeLists.txt does not include all required modules."
    exit 1
fi

echo "All structure tests passed!"
exit 0
