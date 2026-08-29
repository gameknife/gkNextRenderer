file(GLOB_RECURSE scadStudioSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

gk_add_application(ScadStudio
    SOURCES ${scadStudioSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
        NextAI
    LINK NextGameplay
)
