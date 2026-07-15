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
gk_configure_application(gkNextMotionBenchmark MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
)
target_include_directories(gkNextMotionBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gkNextMotionBenchmark PRIVATE NextGameplay)
