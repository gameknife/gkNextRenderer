file(GLOB_RECURSE gkNextMassiveBenchmarkSources CONFIGURE_DEPENDS
    "*.cpp"
    "*.hpp"
    "*.h"
)

add_executable(gkNextMassiveBenchmark
    ${gkNextMassiveBenchmarkSources}
    ${GK_BENCHMARK_COMMON_SOURCES}
    ${GK_APPLICATION_COMMON_SOURCES}
    ${GK_DESKTOP_MAIN_SOURCE}
)
gk_configure_application(gkNextMassiveBenchmark MINIMAL_LINK MODULES
    NextPhysics
    NextTemporalUpscaler
)
target_include_directories(gkNextMassiveBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
