file(GLOB_RECURSE scadLibrarySources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(ScadLibrary
    ${scadLibrarySources}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(ScadLibrary MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(ScadLibrary PRIVATE NextGameplay)
