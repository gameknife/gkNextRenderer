set(GK_DESKTOP_MAIN_SOURCE ${GK_SOURCE_ROOT}/DesktopMain.cpp)

file(GLOB_RECURSE GK_APPLICATION_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/*.h"
)

add_subdirectory(Render)

if(NOT ANDROID AND NOT IOS)
    add_subdirectory(Editor)
    add_subdirectory(Game)
    add_subdirectory(Util)
endif()
