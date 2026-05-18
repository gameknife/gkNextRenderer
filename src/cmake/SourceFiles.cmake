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
    "ThirdParty/mikktspace/*.c"
    "ThirdParty/mikktspace/*.h"
    "ThirdParty/miniaudio/*.h"
    "ThirdParty/lzav/*.h"
    "ThirdParty/tinybvh/*.h"
    "ThirdParty/ImGuizmo/*.cpp"
    "ThirdParty/ImGuizmo/*.h"
    "ThirdParty/ImAnim/*.cpp"
    "ThirdParty/ImAnim/*.h"
    "ThirdParty/imgui-custom/imgui_impl_sdl3_custom.cpp"
    "ThirdParty/imgui-custom/imgui_impl_sdl3_custom.h"
    "ThirdParty/ozz/*.h"
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

# --- Gameplay Layer ---
file(GLOB_RECURSE src_files_nextgameplay
    "NextGameplay/*.cpp"
    "NextGameplay/*.hpp"
    "NextGameplay/*.h"
)

# --- Editor ---
file(GLOB_RECURSE src_files_editor "Application/Editor/gkNextEditor/*")

# --- Applications ---
file(GLOB_RECURSE src_files_magicalego
    "Application/Game/MagicaLego/*.cpp"
    "Application/Game/MagicaLego/*.hpp"
)

file(GLOB_RECURSE src_files_brickplayer
    "Application/Game/BrickPlayer/*.cpp"
    "Application/Game/BrickPlayer/*.hpp"
)

file(GLOB_RECURSE src_files_gkrenderer "Application/Render/gkNextRenderer/*")

file(GLOB_RECURSE src_files_benchmarkcommon "Application/Render/gkNextBenchmark/Common/*")

file(GLOB_RECURSE src_files_gkstillbenchmark "Application/Render/gkNextBenchmark/gkNextStillBenchmark/*")

file(GLOB_RECURSE src_files_gkmotionbenchmark "Application/Render/gkNextBenchmark/gkNextMotionBenchmark/*")

file(GLOB_RECURSE src_files_gkvisualtest "Application/Render/gkNextVisualTest/*"
)

file(GLOB_RECURSE src_files_konglie3d
    "Application/Game/KongLie3D/*.cpp"
    "Application/Game/KongLie3D/*.hpp"
)

file(GLOB_RECURSE src_files_brotato3d
    "Application/Game/Brotato3D/*.cpp"
    "Application/Game/Brotato3D/*.hpp"
)

file(GLOB_RECURSE src_files_flappycpp
    "Application/Game/Flappy/FlappyCommon.hpp"
    "Application/Game/Flappy/FlappyConfig.cpp"
    "Application/Game/Flappy/FlappyConfig.hpp"
    "Application/Game/Flappy/FlappyCpp/*.cpp"
    "Application/Game/Flappy/FlappyCpp/*.hpp"
)

file(GLOB_RECURSE src_files_flappyjs
    "Application/Game/Flappy/FlappyJs/*.cpp"
    "Application/Game/Flappy/FlappyJs/*.hpp"
)

file(GLOB_RECURSE src_files_characterdemo
    "Application/Game/CharacterDemo/*.cpp"
    "Application/Game/CharacterDemo/*.hpp"
)
