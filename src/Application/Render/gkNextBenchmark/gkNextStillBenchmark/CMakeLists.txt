file(GLOB_RECURSE gkNextStillBenchmarkSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)
file(GLOB_RECURSE gkNextBenchmarkCommonSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/*.h"
)

# Intentionally mirrors the minimal renderer module footprint. DemoScenes need no asset loaders.
gk_add_application(gkNextStillBenchmark MINIMAL_LINK
    SOURCES
        ${gkNextStillBenchmarkSources}
        ${gkNextBenchmarkCommonSources}
        ${GK_APPLICATION_COMMON_SOURCES}
    MODULES
        SceneContent
        NextValidation
        NextCapture
        NextPhysics
        NextTemporalUpscaler
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..
)
