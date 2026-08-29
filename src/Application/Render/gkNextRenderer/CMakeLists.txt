file(GLOB_RECURSE gkNextRendererSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

set(gkNextRendererModules
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
    SplatLoader
    RenderViews
)
if(GK_ENABLE_VITURE)
    list(APPEND gkNextRendererModules NextViture)
endif()

gk_add_application(gkNextRenderer
    SOURCES
        ${gkNextRendererSources}
        ${GK_APPLICATION_COMMON_SOURCES}
    MODULES ${gkNextRendererModules}
    LINK NextGameplay
)
