include_guard(GLOBAL)

option(GK_WITH_REMOTE "Enable WebRTC remote play support" ON)

set(GK_REMOTE_ENABLED OFF)
set(GK_REMOTE_LINK_LIBS "")

if(GK_WITH_REMOTE AND NOT (ANDROID OR IOS OR APPLE))
    find_package(LibDataChannel CONFIG QUIET)
    find_package(httplib CONFIG QUIET)

    if(TARGET LibDataChannel::LibDataChannel)
        set(GK_REMOTE_LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannel)
    elseif(TARGET LibDataChannel::LibDataChannelStatic)
        set(GK_REMOTE_LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannelStatic)
    endif()

    if(GK_REMOTE_LIBDATACHANNEL_TARGET AND TARGET httplib::httplib)
        set(GK_REMOTE_ENABLED ON)
        list(APPEND GK_REMOTE_LINK_LIBS
            ${GK_REMOTE_LIBDATACHANNEL_TARGET}
            httplib::httplib
        )
        message(STATUS "Remote Play enabled: ${GK_REMOTE_LIBDATACHANNEL_TARGET} + httplib::httplib")
    else()
        message(STATUS "Remote Play disabled: libdatachannel/cpp-httplib targets not found")
    endif()
else()
    message(STATUS "Remote Play disabled for this platform or by GK_WITH_REMOTE=OFF")
endif()
