set(GK_FLAPPY_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/FlappyCommon.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FlappyConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FlappyConfig.hpp
)

add_subdirectory(FlappyCpp)
if(GK_DOTNET_ENABLED)
    add_subdirectory(FlappyCSharp)
endif()
