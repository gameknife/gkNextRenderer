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
# DemoScenes need no asset loaders, but the benchmark overlay requires the ImGui backend.
gk_configure_application(gkNextMotionBenchmark MINIMAL_LINK MODULES
    SceneContent
    NextValidation
    NextUI
    NextCapture
    NextPhysics
    NextTemporalUpscaler
)
target_include_directories(gkNextMotionBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
