file(GLOB_RECURSE gkNextMotionBenchmarkSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)
file(GLOB_RECURSE gkNextBenchmarkCommonSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Common/*.h"
)

# DemoScenes need no asset loaders, but the benchmark overlay requires the ImGui backend.
gk_add_application(gkNextMotionBenchmark MINIMAL_LINK
    SOURCES
        ${gkNextMotionBenchmarkSources}
        ${gkNextBenchmarkCommonSources}
        ${GK_APPLICATION_COMMON_SOURCES}
    MODULES
        SceneContent
        NextValidation
        NextUI
        NextCapture
        NextPhysics
        NextTemporalUpscaler
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..
)
