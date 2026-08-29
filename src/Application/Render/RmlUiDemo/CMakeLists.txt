file(GLOB_RECURSE rmlUiDemoSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

gk_add_application(RmlUiDemo
    SOURCES ${rmlUiDemoSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        NextRmlUi
)
