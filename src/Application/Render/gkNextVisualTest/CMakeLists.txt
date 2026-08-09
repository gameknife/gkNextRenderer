file(GLOB_RECURSE gkNextVisualTestSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(gkNextVisualTest
    ${gkNextVisualTestSources}
    ${GK_APPLICATION_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(gkNextVisualTest MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
    SplatLoader
)
