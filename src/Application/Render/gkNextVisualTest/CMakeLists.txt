file(GLOB_RECURSE gkNextVisualTestSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

gk_add_application(gkNextVisualTest
    SOURCES
        ${gkNextVisualTestSources}
        ${GK_APPLICATION_COMMON_SOURCES}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        LDrawLoader
        ScadLoader
        SplatLoader
)
