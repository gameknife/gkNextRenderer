set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(GK_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

include(cmake/SourceFiles.cmake)
include(cmake/TargetHelpers.cmake)
include(cmake/RuntimeFeatures.cmake)

if(IOS)
    set_source_files_properties(
        ${GK_SOURCE_ROOT}/Engine/Runtime/Subsystems/NextAudio.cpp
        PROPERTIES LANGUAGE OBJCXX
    )
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

add_subdirectory(ThirdParty/ozz)
if(NOT ANDROID)
    add_subdirectory(ThirdParty/quickjs-ng)
endif()

if(NOT ANDROID)
    add_subdirectory(Engine)
    add_subdirectory(Modules)
    add_subdirectory(Gameplay)
endif()

# DesktopMain installs this shared set of runtime services. Optional modules
# that do not exist on the active platform are omitted here, not special-cased
# in every application.
set(GK_STANDARD_RUNTIME_MODULES
    DevTools
    LiveCoding
    NextAudio
    NextPhysics
    NextStreamline
    NextFidelityFX
    NextTemporalUpscaler
    GltfLoader
    NextRemote
)
if(TARGET NextTui)
    list(APPEND GK_STANDARD_RUNTIME_MODULES NextTui)
endif()

add_subdirectory(Application)

if(NOT ANDROID AND NOT IOS)
    add_subdirectory(Tests)
endif()

if(TARGET quickjs)
    set_target_properties(quickjs PROPERTIES FOLDER "ThirdParty")
endif()
set_target_properties(ozz PROPERTIES FOLDER "ThirdParty")
set_target_properties(Assets PROPERTIES FOLDER "Engine")
