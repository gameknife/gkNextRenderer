# ============================================================================
# SourceFiles.cmake - Source file definitions for gkNextRenderer
# ============================================================================
# Centralized source file configuration using GLOB_RECURSE.
# ============================================================================

# --- Assets ---
file(GLOB_RECURSE src_files_assets
    "Assets/*.cpp"
    "Assets/*.hpp"
    "Assets/*.h"
)

# --- Utilities ---
file(GLOB_RECURSE src_files_utilities
    "Utilities/*.cpp"
    "Utilities/*.hpp"
    "Utilities/*.h"
)

# --- Vulkan Backend ---
file(GLOB_RECURSE src_files_vulkan
    "Vulkan/*.cpp"
    "Vulkan/*.hpp"
)

# --- Rendering ---
file(GLOB_RECURSE src_files_rendering
    "Rendering/*.cpp"
    "Rendering/*.hpp"
)

# --- ThirdParty Libraries ---
file(GLOB_RECURSE src_files_thirdparty
    "ThirdParty/json11/*.cpp"
    "ThirdParty/json11/*.hpp"
    "ThirdParty/mikktspace/*.c"
    "ThirdParty/mikktspace/*.h"
    "ThirdParty/miniaudio/*.h"
    "ThirdParty/lzav/*.h"
    "ThirdParty/tinybvh/*.h"
    "ThirdParty/ImGuizmo/*.cpp"
    "ThirdParty/ImGuizmo/*.h"
    "ThirdParty/ozz/*.h"
)

# --- Custom ImGui Backend ---
file(GLOB_RECURSE src_files_customimgui
    "ThirdParty/imgui-custom/*.cpp"
    "ThirdParty/imgui-custom/*.h"
)

# --- Engine Core ---
file(GLOB_RECURSE src_files_engine
    "Common/*.hpp"
    "Runtime/*.h"
    "Runtime/*.hpp"
    "Runtime/*.cpp"
    "Options.cpp"
    "Options.hpp"
)

# --- Editor ---
file(GLOB_RECURSE src_files_editor "Editor/*")

# --- Applications ---
file(GLOB_RECURSE src_files_magicalego
    "Application/MagicaLego/*.cpp"
    "Application/MagicaLego/*.hpp"
)

file(GLOB_RECURSE src_files_gkrenderer "Application/gkNextRenderer/*")

file(GLOB_RECURSE src_files_benchmarkcommon "Application/gkNextBenchmark/Common/*")

file(GLOB_RECURSE src_files_gkstillbenchmark "Application/gkNextBenchmark/gkNextStillBenchmark/*")

file(GLOB_RECURSE src_files_gkmotionbenchmark "Application/gkNextBenchmark/gkNextMotionBenchmark/*")

file(GLOB_RECURSE src_files_gkvisualtest "Application/gkNextVisualTest/*"
)
