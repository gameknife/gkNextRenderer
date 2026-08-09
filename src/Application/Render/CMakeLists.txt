add_subdirectory(gkNextRenderer)

if(NOT ANDROID AND NOT IOS)
    add_subdirectory(gkNextMinimalRenderer)
    add_subdirectory(gkNextBenchmark)
    add_subdirectory(gkNextVisualTest)
    add_subdirectory(RmlUiDemo)
endif()
