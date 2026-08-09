file(GLOB_RECURSE scadStudioSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(ScadStudio
    ${scadStudioSources}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(ScadStudio MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(ScadStudio PRIVATE NextAI NextGameplay)
