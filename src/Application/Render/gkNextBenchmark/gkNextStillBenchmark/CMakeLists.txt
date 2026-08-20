file(GLOB_RECURSE gkNextStillBenchmarkSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(gkNextStillBenchmark
    ${gkNextStillBenchmarkSources}
    ${GK_BENCHMARK_COMMON_SOURCES}
    ${GK_APPLICATION_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
# Intentionally mirrors the minimal renderer module footprint. DemoScenes need no asset loaders.
gk_configure_application(gkNextStillBenchmark MINIMAL_LINK MODULES
    SceneContent
    NextValidation
    NextCapture
    NextPhysics
    NextTemporalUpscaler
)
target_include_directories(gkNextStillBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
