# Getting Started Guide

`gkNextEngine` is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan. Through the unified `gnb` CLI toolchain, dependency management, environment setup, compilation, and testing are streamlined into single-line commands.

---

## 🛠️ Prerequisites

| Platform | Compiler / Host Tools | Notes |
| :--- | :--- | :--- |
| **Windows** | Visual Studio 2022 (C++ workload) | Builds default to the fast Ninja generator |
| **Linux** | GCC 12+ or Clang 16+, CMake 3.26+, Ninja | `gnb setup` auto-installs missing system packages on Ubuntu / Arch |
| **macOS** | Xcode / Command Line Tools (Apple Silicon) | Native arm64 architecture support |

> **Note**: No need to manually install Vulkan SDK or Slang compiler. `gnb setup` will automatically download and configure the exact project SDK versions locally.

---

## 🚀 One-Click Build & Run

### 1. Host Environment Doctor
```bash
./gnb.bat doctor    # Windows
./gnb.sh doctor     # Linux / macOS
```

### 2. Setup Dependencies
```bash
./gnb.bat setup     # Windows
./gnb.sh setup      # Linux / macOS
```

### 3. Fast Incremental Build
```bash
# Default: build core targets (gkNextRenderer + gkNextUnitTests)
./gnb.bat build

# Build all 15+ subprojects
./gnb.bat build --all

# Build a specific subproject (e.g. MagicaLego)
./gnb.bat build MagicaLego
```

### 4. Run & Experience
```bash
# Run main path tracing renderer
./gnb.bat run gkNextRenderer

# Run comprehensive editor
./gnb.bat run gkNextEditor

# Run AirportSim simulation
./gnb.bat run AirportSim
```

---

## 🌐 Remote Play via WebRTC

Any desktop target natively supports running as a WebRTC host, encoding 60FPS video through Vulkan Video hardware encoding:

```bash
./gnb.sh remote --target gkNextRenderer --scene assets/models/playground.glb --res 1280x720
```

Open the printed URL (e.g., `http://127.0.0.1:8080`) in any modern browser to play with zero installation.
