#!/bin/bash

INPUT_PATH="$1"

if [ -z "$INPUT_PATH" ]; then
    echo "Usage: $0 <path-to-CMakeLists.txt-or-directory>"
    exit 1
fi

if [ ! -e "$INPUT_PATH" ]; then
    echo "Error: Path '$INPUT_PATH' not found."
    exit 1
fi

if [ -d "$INPUT_PATH" ]; then
    mapfile -t CMAKE_FILES < <(
        find "$INPUT_PATH" \
            \( -type d \( \
                -name .git -o \
                -name .vcpkg -o \
                -name out -o \
                -name external -o \
                -name ThirdParty -o \
                -name vcpkg-overlays -o \
                -name custom-triplets -o \
                -path '*/tests/build_system/fixtures' \
            \) -prune \) -o \
            \( -type f \( -name CMakeLists.txt -o -name '*.cmake' \) -print \)
    )
else
    CMAKE_FILES=("$INPUT_PATH")
fi

# forbidden_patterns array
# Format: "pattern description"
forbidden_patterns=(
    "^\s*include_directories\("
    "^\s*link_libraries\("
    "^\s*link_directories\("
    "^\s*add_definitions\("
    "^\s*set\s*\(\s*CMAKE_(C|CXX|EXE_LINKER)_FLAGS"
)

found_errors=0

for file_path in "${CMAKE_FILES[@]}"; do
    for pattern in "${forbidden_patterns[@]}"; do
        if grep -Eq "$pattern" "$file_path"; then
            echo "Error: Forbidden pattern '$pattern' found in '$file_path'."
            echo "  -> Please use target-specific commands (e.g., target_include_directories)."
            grep -En "$pattern" "$file_path"
            found_errors=1
        fi
    done
done

if [ $found_errors -ne 0 ]; then
    exit 1
fi

exit 0
