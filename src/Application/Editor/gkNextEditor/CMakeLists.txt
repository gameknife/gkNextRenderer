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
gk_configure_application(gkNextEditor MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
    SplatLoader
    NextQuickJS
    RenderViews
    SceneExport
)
target_link_libraries(gkNextEditor PRIVATE NextGameplay)

set_source_files_properties(
    Automation/EditorScriptExecutor.cpp
    Automation/ScriptConsole.cpp
    Panels/PropertyWidgets.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

if(NOT MSVC)
    set_source_files_properties(${gkNextEditorSources} PROPERTIES COMPILE_OPTIONS -w)
endif()
