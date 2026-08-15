# ============================================================================
# SourceFiles.cmake - Source file definitions for gkNextRenderer
# ============================================================================
# Core and module source collections shared by the normal library layout and
# Android's intentionally monolithic shared-library layout. Application sources
# live beside each application's own CMakeLists.txt.
# ============================================================================

# --- Assets ---
file(GLOB_RECURSE src_files_assets CONFIGURE_DEPENDS
    "Engine/Assets/*.cpp"
    "Engine/Assets/*.hpp"
    "Engine/Assets/*.h"
)

# --- Utilities ---
file(GLOB_RECURSE src_files_utilities CONFIGURE_DEPENDS
    "Engine/Utilities/*.cpp"
    "Engine/Utilities/*.hpp"
    "Engine/Utilities/*.h"
)

# --- Vulkan Backend ---
file(GLOB_RECURSE src_files_vulkan CONFIGURE_DEPENDS
    "Engine/Vulkan/*.cpp"
    "Engine/Vulkan/*.hpp"
)

# --- Rendering ---
file(GLOB_RECURSE src_files_rendering CONFIGURE_DEPENDS
    "Engine/Rendering/*.cpp"
    "Engine/Rendering/*.hpp"
)

# --- ThirdParty Libraries ---
file(GLOB_RECURSE src_files_thirdparty CONFIGURE_DEPENDS
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
file(GLOB_RECURSE src_files_engine CONFIGURE_DEPENDS
    "Engine/Common/*.hpp"
    "Engine/Runtime/*.h"
    "Engine/Runtime/*.hpp"
    "Engine/Runtime/*.cpp"
    "Engine/Options.cpp"
    "Engine/Options.hpp"
)

# --- Gameplay Layer ---
file(GLOB_RECURSE src_files_nextgameplay CONFIGURE_DEPENDS
    "Gameplay/*.cpp"
    "Gameplay/*.hpp"
    "Gameplay/*.h"
)

# --- Optional Engine Modules (src/Modules/<Name>, one static library each) ---
set(GK_MODULE_NAMES GltfLoader LDrawLoader ScadLoader SplatLoader NextAI NextAudio NextPhysics NextRemote NextRmlUi DevTools LiveCoding RenderViews NextStreamline NextFidelityFX NextTemporalUpscaler SceneExport)
if(GK_ENABLE_VITURE)
    list(APPEND GK_MODULE_NAMES NextViture)
endif()
if(GK_WITH_TUI AND NOT (ANDROID OR IOS))
    list(APPEND GK_MODULE_NAMES NextTui)
endif()
if(GK_DOTNET_ENABLED)
    list(APPEND GK_MODULE_NAMES NextDotNet)
endif()
foreach(gk_module IN LISTS GK_MODULE_NAMES)
    file(GLOB_RECURSE src_files_module_${gk_module} CONFIGURE_DEPENDS
        "Modules/${gk_module}/*.cpp"
        "Modules/${gk_module}/*.hpp"
        "Modules/${gk_module}/*.h"
    )
endforeach()

# Two NextDotNet subdirectories are deliberately not part of the module library: Probe/ is a
# standalone acceptance harness with its own main() and CMake project, and Stub/ is linked only
# into targets that use the module without hosting managed code.
if(src_files_module_NextDotNet)
    list(FILTER src_files_module_NextDotNet EXCLUDE REGEX "/(Probe|Stub)/")
endif()
set(src_files_modules_all "")
foreach(gk_module IN LISTS GK_MODULE_NAMES)
    list(APPEND src_files_modules_all ${src_files_module_${gk_module}})
endforeach()
