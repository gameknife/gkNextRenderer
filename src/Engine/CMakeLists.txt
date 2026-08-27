add_library(gkNextEngine
    ${src_files_assets}
    ${src_files_utilities}
    ${src_files_engine}
    ${src_files_vulkan}
    ${src_files_rendering}
    ${src_files_thirdparty}
)

gk_apply_target_defaults(gkNextEngine)
set_target_properties(gkNextEngine PROPERTIES FOLDER "Engine")
target_compile_definitions(gkNextEngine PRIVATE
    ENGINE_EXPORTS
    GK_NEXT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

file(TO_CMAKE_PATH "${Vulkan_SLANGC}" GK_SLANGC_EXECUTABLE_PATH)
target_compile_definitions(gkNextEngine PRIVATE
    GK_SLANGC_EXECUTABLE="${GK_SLANGC_EXECUTABLE_PATH}"
)

if(ENABLE_UNITY_BUILD)
    set_target_properties(gkNextEngine PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 12
    )
    set_source_files_properties(${src_files_assets} PROPERTIES UNITY_GROUP "assets")
    set_source_files_properties(${src_files_utilities} PROPERTIES UNITY_GROUP "utilities")
    set_source_files_properties(${src_files_engine} PROPERTIES UNITY_GROUP "engine")
    set_source_files_properties(${src_files_vulkan} PROPERTIES UNITY_GROUP "vulkan")
    set_source_files_properties(${src_files_thirdparty} PROPERTIES UNITY_GROUP "thirdparty")

    set_source_files_properties(
        ${GK_SOURCE_ROOT}/Engine/Vulkan/Allocator.cpp
        ${GK_SOURCE_ROOT}/Engine/Utilities/StbImage.cpp
        ${GK_SOURCE_ROOT}/Engine/Assets/Acceleration/CpuBvh.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

if(MSVC)
    set_source_files_properties(${src_files_thirdparty} PROPERTIES COMPILE_OPTIONS /W0)
else()
    set_source_files_properties(${src_files_thirdparty} PROPERTIES COMPILE_OPTIONS -w)
endif()

find_package(WebP CONFIG REQUIRED)
target_link_libraries(gkNextEngine PRIVATE
    SDL3::SDL3
    xxHash::xxhash
    meshoptimizer::meshoptimizer
    fmt::fmt
    glm::glm
    imgui::imgui
    draco::draco
    WebP::webp
    WebP::libwebpmux
    GPUOpen::VulkanMemoryAllocator
    ${extra_libs}
)
target_link_libraries(gkNextEngine PUBLIC spdlog::spdlog)

if(NOT IOS)
    target_link_libraries(gkNextEngine PRIVATE CURL::libcurl)
endif()

if(IOS)
    get_target_property(imguiInterfaces imgui::imgui INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM imguiInterfaces Vulkan::Vulkan)
    set_target_properties(imgui::imgui PROPERTIES
        INTERFACE_LINK_LIBRARIES "${imguiInterfaces}"
    )
    target_link_libraries(gkNextEngine PRIVATE ${MOLTENVK_LIBRARY})
elseif(WIN32 AND WITH_STREAMLINE)
    get_target_property(imguiInterfaces imgui::imgui INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM imguiInterfaces Vulkan::Vulkan)
    set_target_properties(imgui::imgui PROPERTIES
        INTERFACE_LINK_LIBRARIES "${imguiInterfaces}"
    )
    target_link_directories(gkNextEngine PRIVATE ${STREAMLINE_LIB_DIR})
    target_link_libraries(gkNextEngine PRIVATE
        "${STREAMLINE_LIB_DIR}/sl.interposer.lib"
    )
else()
    target_link_libraries(gkNextEngine PRIVATE ${Vulkan_LIBRARIES})
endif()

if(NOT IOS)
    target_link_libraries(gkNextEngine PRIVATE cpptrace::cpptrace)
endif()

if(WITH_STREAMLINE)
    target_compile_definitions(gkNextEngine PUBLIC WITH_STREAMLINE=1)
    target_compile_definitions(gkNextEngine PRIVATE
        GK_STREAMLINE_INTERPOSER_DLL="${STREAMLINE_BIN_DIR}/sl.interposer.dll"
    )
    target_include_directories(gkNextEngine PRIVATE ${STREAMLINE_INCLUDE_DIR})
    target_link_directories(gkNextEngine PRIVATE ${STREAMLINE_LIB_DIR})

    file(GLOB streamlineBinaries CONFIGURE_DEPENDS "${STREAMLINE_BIN_DIR}/*")
    file(COPY ${streamlineBinaries} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()

if(WIN32)
    target_link_libraries(gkNextEngine PUBLIC ws2_32)
endif()

gk_link_engine_feature_dependencies(gkNextEngine)
