cmake_minimum_required(VERSION 3.25)
project(InvalidGlobals)

# This should fail validation
include_directories(include)
link_libraries(some_lib)

add_executable(app main.cpp)
