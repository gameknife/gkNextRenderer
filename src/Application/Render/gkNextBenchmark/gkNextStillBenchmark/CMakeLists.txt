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
gk_configure_application(gkNextStillBenchmark MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    LDrawLoader
    ScadLoader
)
target_include_directories(gkNextStillBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
