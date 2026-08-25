file(GLOB_RECURSE gkNextEditorSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(gkNextEditor
    ${gkNextEditorSources}
    ${GK_EDITOR_COMMON_SOURCES}
    ${GK_APPLICATION_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
set(gkNextEditorModules
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
    SplatLoader
    RenderViews
    SceneExport
)
if(GK_ENABLE_VITURE)
    list(APPEND gkNextEditorModules NextViture)
endif()
# Play-in-editor runs a C# game inside the editor process. Optional: where no .NET SDK was found
# the module does not exist and FPlaySession degrades to "unavailable" rather than disappearing.
if(TARGET NextDotNet)
    list(APPEND gkNextEditorModules NextDotNet)
endif()
gk_configure_application(gkNextEditor MODULES ${gkNextEditorModules})
target_link_libraries(gkNextEditor PRIVATE NextGameplay)

if(GK_DOTNET_ENABLED)
    # The editor hosts games chosen at runtime and brings no C# of its own. Under NativeAOT this
    # supplies the bootstrap symbol the linker wants; play-in-editor then reports itself unavailable
    # at runtime, because an AOT binary has exactly one game linked in and nothing to choose.
    gk_dotnet_stub_game(gkNextEditor)
endif()

set_source_files_properties(
    Automation/EditorScriptExecutor.cpp
    Automation/ScriptConsole.cpp
    Panels/PropertyWidgets.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

if(NOT MSVC)
    set_source_files_properties(${gkNextEditorSources} PROPERTIES COMPILE_OPTIONS -w)
endif()
