project(quickjs)

file(STRINGS VERSION version)
set(quickjs_src quickjs/quickjs.c quickjs/libbf.c quickjs/libunicode.c quickjs/libregexp.c quickjs/cutils.c quickjs/quickjs-libc.c quickjspp.cpp quickjspp.hpp)
set(quickjs_def CONFIG_VERSION="${version}" _GNU_SOURCE CONFIG_BIGNUM)

add_library(quickjs ${quickjs_src})

target_compile_definitions(quickjs PRIVATE ${quickjs_def} )

if (MSVC)
target_compile_options(quickjs PRIVATE /W0)
else()
target_compile_options(quickjs PRIVATE -w)
endif()

if(UNIX OR MINGW)
  find_package(Threads)
  target_link_libraries(quickjs ${CMAKE_DL_LIBS} m Threads::Threads)
endif()
