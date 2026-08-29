file(GLOB_RECURSE flappyCppSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

# FlappyConfig is the C++ game's own config reader. The C# game reads the same JSON through its own
# FlappyConfigLoader: sharing this implementation would give the parity pair a common one exactly
# where the two are supposed to be independent.
set(flappyCommonSources
    ${CMAKE_CURRENT_SOURCE_DIR}/../FlappyCommon.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../FlappyConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../FlappyConfig.hpp
)

gk_add_application(FlappyCpp
    SOURCES
        ${flappyCppSources}
        ${flappyCommonSources}
    MODULES ${GK_STANDARD_RUNTIME_MODULES}
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..
)
