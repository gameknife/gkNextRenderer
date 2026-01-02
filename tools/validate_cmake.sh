#!/bin/bash

FILE_PATH="$1"

if [ -z "$FILE_PATH" ]; then
    echo "Usage: $0 <path-to-CMakeLists.txt>"
    exit 1
fi

if [ ! -f "$FILE_PATH" ]; then
    echo "Error: File '$FILE_PATH' not found."
    exit 1
fi

# forbidden_patterns array
# Format: "pattern description"
forbidden_patterns=(
    "^\s*include_directories\("
    "^\s*link_libraries\("
    "^\s*link_directories\("
    "^\s*add_definitions\("
)

found_errors=0

for pattern in "${forbidden_patterns[@]}"; do
    if grep -Eq "$pattern" "$FILE_PATH"; then
        echo "Error: Forbidden pattern '$pattern' found in '$FILE_PATH'."
        echo "  -> Please use target-specific commands (e.g., target_include_directories)."
        grep -En "$pattern" "$FILE_PATH"
        found_errors=1
    fi
done

if [ $found_errors -ne 0 ]; then
    exit 1
fi

exit 0
