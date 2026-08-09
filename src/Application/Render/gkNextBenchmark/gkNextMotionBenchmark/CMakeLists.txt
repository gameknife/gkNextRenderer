file(GLOB_RECURSE gkNextMotionBenchmarkSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(gkNextMotionBenchmark
    ${gkNextMotionBenchmarkSources}
    ${GK_BENCHMARK_COMMON_SOURCES}
    ${GK_APPLICATION_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
# Intentionally mirrors the minimal renderer module footprint. DemoScenes need no asset loaders.
gk_configure_application(gkNextMotionBenchmark MINIMAL_LINK MODULES
    NextPhysics
    NextTemporalUpscaler
)
target_include_directories(gkNextMotionBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
