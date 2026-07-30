file(GLOB_RECURSE GK_BENCHMARK_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.h"
)

add_subdirectory(gkNextStillBenchmark)
add_subdirectory(gkNextMotionBenchmark)
add_subdirectory(gkNextMassiveBenchmark)
