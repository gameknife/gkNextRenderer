#!/bin/bash
set -e

detect_platform() {
    case "$(uname -s)" in
        Darwin*)
            echo "macos"
            ;;
        Linux*)
            # Check if it's Arch Linux
            if [ -f "/etc/arch-release" ] || [ -f "/etc/manjaro-release" ] || [ -f "/etc/endeavouros-release" ]; then
                echo "archlinux"
            else
                echo "linux"
            fi
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

setup_archlinux() {
    echo "Setting up Arch Linux environment..."
    sudo pacman -S base-devel ninja zip unzip tar cmake
    sudo pacman -S ttf-cascadia-code otf-cascadia-code
    echo "Arch Linux setup completed!"
}

setup_linux() {
    echo "Setting up Linux environment..."
    sudo apt-get update
    sudo apt-get install -y curl unzip tar ninja-build
    echo "Linux setup completed!"
}

setup_macos() {
    echo "macOS setup: No automatic setup yet"
    echo "Please ensure Homebrew is installed and run:"
    echo "brew install ninja curl"
}

setup_windows() {
    echo "Windows setup: No automatic setup yet"
    echo "Please ensure Visual Studio 2022 with C++ tools is installed"
}

show_help() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  setup     Setup development environment for current platform"
    echo "  help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 setup     # Auto-detect platform and setup"
}

main() {
    local command="$1"

    if [ -z "$command" ]; then
        show_help
        exit 1
    fi

    case "$command" in
        setup)
            platform=$(detect_platform)
            case "$platform" in
                macos)
                    setup_macos
                    ;;
                linux)
                    setup_linux
                    ;;
                archlinux)
                    setup_archlinux
                    ;;
                windows)
                    setup_windows
                    ;;
                *)
                    echo "Error: Unsupported platform '$platform'"
                    exit 1
                    ;;
            esac
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            echo "Error: Unknown command '$command'"
            show_help
            exit 1
            ;;
    esac
}

main "$@"