set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR})
if(ANDROID)
    if(NOT GK_ANDROID_ASSET_OUTPUT_DIR)
        message(FATAL_ERROR
            "GK_ANDROID_ASSET_OUTPUT_DIR is required for Android builds. "
            "Configure Android through tools/android so assets stay in the build tree.")
    endif()
    get_filename_component(output_base_dir "${GK_ANDROID_ASSET_OUTPUT_DIR}" ABSOLUTE)
elseif(IOS)
    set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR}/assets/)
endif()

set(ASSET_DIRS
    anims
    brand
    configs
    fonts
    legos
    locale
    models
    omr
    paks
    remote
    rmlui_demo
    scad
    sog
    scripts
    sfx
    sounds
    textures
)

# Android packages the generated asset tree directly through Gradle. Keep that
# tree exact across CMake regenerations so removed source assets cannot survive
# in a later APK. The stamps are removed together with their destinations so the
# copy commands below repopulate every directory after the cleanup.
if(ANDROID)
    foreach(dir IN LISTS ASSET_DIRS)
        file(REMOVE_RECURSE "${output_base_dir}/${dir}")
        file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/${dir}.stamp")
    endforeach()
endif()

set(all_asset_files "")
set(all_asset_stamps "")

foreach(dir IN LISTS ASSET_DIRS)
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    if(NOT EXISTS "${src_dir}")
        message(STATUS "Asset folder '${dir}' not found at ${src_dir}; skipping copy. Run 'gnb paks fetch' from the repository root (then re-run CMake configure) if you need optional assets.")
        continue()
    endif()

    file(GLOB_RECURSE ${dir}_files CONFIGURE_DEPENDS "${src_dir}/*")
    list(APPEND all_asset_files ${${dir}_files})

    set(${dir}_stamp "${CMAKE_CURRENT_BINARY_DIR}/${dir}.stamp")
    list(APPEND all_asset_stamps ${${dir}_stamp})
    add_custom_command(
        OUTPUT ${${dir}_stamp}
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${src_dir}"
            "${output_base_dir}/${dir}"
        COMMAND ${CMAKE_COMMAND} -E touch ${${dir}_stamp}
        DEPENDS ${${dir}_files}
        COMMENT "Copying ${dir}..."
    )
endforeach()

if(NOT ANDROID AND NOT IOS AND Vulkan_SLANGC)
    get_filename_component(slangc_tool_dir "${Vulkan_SLANGC}" DIRECTORY)
    file(GLOB slang_tool_files CONFIGURE_DEPENDS
        "${slangc_tool_dir}/slangc*"
        "${slangc_tool_dir}/slang*.dll"
        "${slangc_tool_dir}/libslang*"
        "${slangc_tool_dir}/../lib/libslang*"
    )
    if(slang_tool_files)
        set(slang_tool_stamp "${CMAKE_CURRENT_BINARY_DIR}/slang-tool.stamp")
        list(APPEND all_asset_files ${slang_tool_files})
        list(APPEND all_asset_stamps ${slang_tool_stamp})
        add_custom_command(
            OUTPUT ${slang_tool_stamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${output_base_dir}/../tools/slang"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${slang_tool_files}
                "${output_base_dir}/../tools/slang"
            COMMAND ${CMAKE_COMMAND} -E touch ${slang_tool_stamp}
            DEPENDS ${slang_tool_files}
            COMMENT "Copying bundled Slang compiler..."
            VERBATIM
        )
    endif()
endif()

source_group("Assets" FILES ${all_asset_files})
