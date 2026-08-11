cmake_minimum_required(VERSION 3.26)

if(NOT EXISTS "${ADB}")
    message(FATAL_ERROR "adb not found: ${ADB}")
endif()
if(NOT ACTION MATCHES "^(install|run|logcat)$")
    message(FATAL_ERROR "Unsupported Android device action: ${ACTION}")
endif()

execute_process(
    COMMAND "${ADB}" devices
    OUTPUT_VARIABLE devicesOutput
    ERROR_VARIABLE devicesError
    RESULT_VARIABLE devicesResult
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT devicesResult EQUAL 0)
    message(FATAL_ERROR "adb devices failed: ${devicesError}")
endif()
string(REPLACE "\r\n" "\n" devicesOutput "${devicesOutput}")
string(REPLACE "\n" ";" deviceLines "${devicesOutput}")
set(availableDevices "")
foreach(line IN LISTS deviceLines)
    if(line MATCHES "^([^ \t]+)[ \t]+device$")
        list(APPEND availableDevices "${CMAKE_MATCH_1}")
    endif()
endforeach()

list(LENGTH availableDevices deviceCount)
if(SERIAL)
    if(NOT SERIAL IN_LIST availableDevices)
        message(FATAL_ERROR "Requested adb device '${SERIAL}' is not online. adb devices output:\n${devicesOutput}")
    endif()
    set(selectedSerial "${SERIAL}")
elseif(deviceCount EQUAL 1)
    list(GET availableDevices 0 selectedSerial)
elseif(deviceCount EQUAL 0)
    message(FATAL_ERROR "No online Android device found. Start an emulator or connect a device.")
else()
    list(JOIN availableDevices ", " deviceList)
    message(FATAL_ERROR "Multiple Android devices are online (${deviceList}); set GK_ANDROID_SERIAL.")
endif()

set(adbCommand "${ADB}" -s "${selectedSerial}")
if(ACTION STREQUAL "install")
    if(NOT EXISTS "${APK}")
        message(FATAL_ERROR "APK not found: ${APK}")
    endif()
    execute_process(COMMAND ${adbCommand} install -r "${APK}" COMMAND_ERROR_IS_FATAL ANY)
elseif(ACTION STREQUAL "run")
    execute_process(
        COMMAND ${adbCommand} shell am start -n com.gknext.renderer/com.gknext.renderer.GkNextActivity
        COMMAND_ERROR_IS_FATAL ANY
    )
else()
    execute_process(COMMAND ${adbCommand} logcat -s gknext COMMAND_ERROR_IS_FATAL ANY)
endif()
