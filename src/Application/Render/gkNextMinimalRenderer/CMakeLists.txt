file(GLOB_RECURSE gkNextMinimalRendererSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(gkNextMinimalRenderer ${gkNextMinimalRendererSources})
gk_configure_application(gkNextMinimalRenderer MODULES NextTemporalUpscaler MINIMAL_LINK)
