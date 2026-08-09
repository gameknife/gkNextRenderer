file(GLOB_RECURSE GK_EDITOR_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.h"
)

add_subdirectory(gkNextEditor)
add_subdirectory(ScadStudio)
add_subdirectory(ScadLibrary)
