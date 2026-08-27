cmake_minimum_required(VERSION 3.26)

if(NOT EXISTS "${INPUT_APK}")
    message(FATAL_ERROR "Gradle did not produce the expected APK: ${INPUT_APK}")
endif()
get_filename_component(outputDir "${OUTPUT_APK}" DIRECTORY)
file(MAKE_DIRECTORY "${outputDir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${INPUT_APK}" "${OUTPUT_APK}"
    COMMAND_ERROR_IS_FATAL ANY
)
message(STATUS "Android APK: ${OUTPUT_APK}")
