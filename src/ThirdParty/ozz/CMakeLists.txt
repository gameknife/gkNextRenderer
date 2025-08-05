cmake_minimum_required(VERSION 3.10)

# CREATE PROJECT
project(ozz)

#add_compile_definitions(IMGUI_DEFINE_MATH_OPERATORS)

# Platform-specific SIMD definitions
if(APPLE OR ANDROID)
    add_compile_definitions(OZZ_SIMD_ARM_NEON)
endif()

# SET SOURCE FILES FOR PROJECT
file(GLOB_RECURSE _HDRS "include/*.h")
file(GLOB_RECURSE _SRCS "src/*.cpp" "src/*.cc" "src/*.c")

# CREATE LIBRARY FROM SOURCE_FILES
add_library(ozz ${_SRCS} ${_HDRS})

target_include_directories(ozz PUBLIC include include/ozz src)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(ozz PRIVATE -Wno-ignored-attributes)
endif()