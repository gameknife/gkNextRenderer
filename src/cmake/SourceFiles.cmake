# ============================================================================
# SourceFiles.cmake - Source file definitions for gkNextRenderer
# ============================================================================
# Centralized source file configuration using GLOB_RECURSE.
# ============================================================================

# --- Assets ---
file(GLOB_RECURSE src_files_assets
    "Engine/Assets/*.cpp"
    "Engine/Assets/*.hpp"
    "Engine/Assets/*.h"
)

# --- Utilities ---
file(GLOB_RECURSE src_files_utilities
    "Engine/Utilities/*.cpp"
    "Engine/Utilities/*.hpp"
    "Engine/Utilities/*.h"
)

# --- Vulkan Backend ---
file(GLOB_RECURSE src_files_vulkan
    "Engine/Vulkan/*.cpp"
    "Engine/Vulkan/*.hpp"
)

# --- Rendering ---
file(GLOB_RECURSE src_files_rendering
    "Engine/Rendering/*.cpp"
    "Engine/Rendering/*.hpp"
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
    "Engine/Common/*.hpp"
    "Engine/Runtime/*.h"
    "Engine/Runtime/*.hpp"
    "Engine/Runtime/*.cpp"
    "Engine/Options.cpp"
    "Engine/Options.hpp"
)

# --- Gameplay Layer ---
file(GLOB_RECURSE src_files_nextgameplay
    "Gameplay/*.cpp"
    "Gameplay/*.hpp"
    "Gameplay/*.h"
)

# --- Optional Engine Modules (src/Modules/<Name>, one static library each) ---
set(GK_MODULE_NAMES GltfLoader LDrawLoader ScadLoader SplatLoader NextAI NextAudio NextPhysics NextRemote NextRmlUi DevTools LiveCoding RenderViews NextStreamline SceneExport)
if(GK_WITH_TUI AND NOT (ANDROID OR IOS))
    list(APPEND GK_MODULE_NAMES NextTui)
endif()
if(NOT ANDROID)
    list(APPEND GK_MODULE_NAMES NextQuickJS)
endif()
foreach(gk_module IN LISTS GK_MODULE_NAMES)
    file(GLOB_RECURSE src_files_module_${gk_module}
        "Modules/${gk_module}/*.cpp"
        "Modules/${gk_module}/*.hpp"
        "Modules/${gk_module}/*.h"
    )
endforeach()
set(src_files_modules_all "")
foreach(gk_module IN LISTS GK_MODULE_NAMES)
    list(APPEND src_files_modules_all ${src_files_module_${gk_module}})
endforeach()

# --- Application shared code (demo scenes etc.) ---
file(GLOB_RECURSE src_files_appcommon
    "Application/Common/*.cpp"
    "Application/Common/*.hpp"
)

# --- Editor ---
file(GLOB_RECURSE src_files_editorcommon "Application/Editor/Common/*")
file(GLOB_RECURSE src_files_editor "Application/Editor/gkNextEditor/*")

# --- SCAD Studio (conversational SCAD model generator) ---
file(GLOB_RECURSE src_files_scadstudio "Application/Editor/ScadStudio/*")

# --- SCAD Library (kit browser + compose bench) ---
file(GLOB_RECURSE src_files_scadlibrary "Application/Editor/ScadLibrary/*")

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

# Intentionally core-only renderer used to validate the Engine -> Module ->
# Application boundary. It has its own entry point and does not use
# DESKTOP_MAIN_SOURCES, which installs optional modules.
file(GLOB_RECURSE src_files_gkminimalrenderer "Application/Render/gkNextMinimalRenderer/*")

file(GLOB_RECURSE src_files_benchmarkcommon "Application/Render/gkNextBenchmark/Common/*")

file(GLOB_RECURSE src_files_gkstillbenchmark "Application/Render/gkNextBenchmark/gkNextStillBenchmark/*")

file(GLOB_RECURSE src_files_gkmotionbenchmark "Application/Render/gkNextBenchmark/gkNextMotionBenchmark/*")

file(GLOB_RECURSE src_files_gkvisualtest "Application/Render/gkNextVisualTest/*"
)

file(GLOB_RECURSE src_files_rmluidemo "Application/Render/RmlUiDemo/*")

file(GLOB_RECURSE src_files_konglie3d
    "Application/Game/KongLie3D/*.cpp"
    "Application/Game/KongLie3D/*.hpp"
)

file(GLOB_RECURSE src_files_brotato3d
    "Application/Game/Brotato3D/*.cpp"
    "Application/Game/Brotato3D/*.hpp"
)

file(GLOB_RECURSE src_files_nextra
    "Application/Game/NextRA/*.cpp"
    "Application/Game/NextRA/*.hpp"
    "Application/Game/NextRA/*.h"
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

file(GLOB_RECURSE src_files_truckerdemo
    "Application/Game/TruckerDemo/*.cpp"
    "Application/Game/TruckerDemo/*.hpp"
)

file(GLOB_RECURSE src_files_studiosim
    "Application/Game/StudioSim/*.cpp"
    "Application/Game/StudioSim/*.hpp"
    "Application/Game/StudioSim/*.h"
)

file(GLOB_RECURSE src_files_airportsim
    "Application/Game/AirportSim/*.cpp"
    "Application/Game/AirportSim/*.hpp"
    "Application/Game/AirportSim/*.h"
)

file(GLOB_RECURSE src_files_citysolsim
    "Application/Game/CitySolSim/*.cpp"
    "Application/Game/CitySolSim/*.hpp"
    "Application/Game/CitySolSim/*.h"
)
