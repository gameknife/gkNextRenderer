cmake_minimum_required(VERSION 3.25)
project(ValidTargets)

add_executable(app main.cpp)

# This should pass validation
target_include_directories(app PRIVATE include)
target_link_libraries(app PRIVATE some_lib)
