file(GLOB_RECURSE magicaLegoSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(MagicaLego ${magicaLegoSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(MagicaLego MODULES ${GK_STANDARD_RUNTIME_MODULES})
target_link_libraries(MagicaLego PRIVATE NextAI)

if(MSVC)
    set(ffmpegPath ${GK_SOURCE_ROOT}/ThirdParty/ffmpeg/bin/ffmpeg.exe)
    if(EXISTS ${ffmpegPath})
        file(COPY ${ffmpegPath} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    else()
        message(STATUS "ffmpeg.exe not found at ${ffmpegPath}; run `gnb paks fetch ffmpeg` if you need MagicaLego video capture.")
    endif()
endif()
