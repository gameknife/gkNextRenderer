file(GLOB_RECURSE rmlUiDemoSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(RmlUiDemo
    ${rmlUiDemoSources}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(RmlUiDemo MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    NextRmlUi
)
