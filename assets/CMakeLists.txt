# ============================================================
# 输出目录设置
# ============================================================
set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR})
if (ANDROID)
    set(output_base_dir ${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/assets/assets/)
elseif (IOS)
    set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR}/assets/)
endif()

# ============================================================
# 非 Shader 资源 - 目录拷贝
# ============================================================
set(ASSET_DIRS anims configs fonts legos locale models omr paks sfx sounds textures typescript)

# 收集所有资源文件
set(all_asset_files "")
set(all_asset_stamps "")

foreach(dir ${ASSET_DIRS})
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    # Some asset folders are populated by scripts/fetch-paks (e.g. sfx) and
    # may be absent on a fresh clone. Skip them gracefully; users will see
    # the hint and can fetch on demand.
    if(NOT EXISTS "${src_dir}")
        message(STATUS "Asset folder '${dir}' not found at ${src_dir}; skipping copy. "
                       "Run scripts/fetch-paks --${dir} (then re-run CMake configure) if you need its contents.")
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

# ============================================================
# Runtime tools
# ============================================================
set(tsc_tool_dir "${CMAKE_CURRENT_SOURCE_DIR}/../tools/tsc")
if(EXISTS "${tsc_tool_dir}")
    file(GLOB_RECURSE tsc_tool_files CONFIGURE_DEPENDS "${tsc_tool_dir}/*")
    set(tsc_tool_stamp "${CMAKE_CURRENT_BINARY_DIR}/tsc-tool.stamp")
    list(APPEND all_asset_files ${tsc_tool_files})
    list(APPEND all_asset_stamps ${tsc_tool_stamp})
    add_custom_command(
        OUTPUT ${tsc_tool_stamp}
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${tsc_tool_dir}"
                "${output_base_dir}/../tools/tsc"
        COMMAND ${CMAKE_COMMAND} -E touch ${tsc_tool_stamp}
        DEPENDS ${tsc_tool_files}
        COMMENT "Copying bundled TypeScript compiler..."
    )
else()
    message(STATUS "Bundled TypeScript compiler not found at ${tsc_tool_dir}; QuickJS TS hot reload will be disabled in copied runtime layouts.")
endif()

if(NOT ANDROID AND NOT IOS AND Vulkan_SLANGC)
    get_filename_component(slangc_tool_dir "${Vulkan_SLANGC}" DIRECTORY)
    file(GLOB slang_tool_files CONFIGURE_DEPENDS
        "${slangc_tool_dir}/slangc*"
        "${slangc_tool_dir}/slang*.dll"
        "${slangc_tool_dir}/libslang*"
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

# source_group - 所有非 Shader 资源
source_group("Assets" FILES ${all_asset_files})

# ============================================================
# Shader 编译
# ============================================================
file(GLOB shader_files shaders/*.comp.slang shaders/*.vert.slang shaders/*.frag.slang)
file(GLOB shader_common_files shaders/common/*.h shaders/common/*.slang)

source_group("Shaders" FILES ${shader_files} ${shader_common_files})

set(all_shader_outputs "")

foreach(shader ${shader_files})
    get_filename_component(file_name ${shader} NAME)
    get_filename_component(full_path ${shader} ABSOLUTE)
    set(output_dir ${output_base_dir}/shaders)
    set(output_file ${output_dir}/${file_name}.spv)
    
    list(APPEND all_shader_outputs ${output_file})
    
    set_source_files_properties(${shader} PROPERTIES HEADER_FILE_ONLY TRUE)
    set_source_files_properties(${shader} PROPERTIES VS_TOOL_OVERRIDE "None")

    set(slangc_args "")
    if(WIN32)
        set(slangc_args "-DSHADER_CLOCK")
    endif()
    if(APPLE)
        set(slangc_args "-DPLATFORM_APPLE")
    endif()
    if(ANDROID)
        set(slangc_args "-DPLATFORM_ANDROID")
    endif()

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
        COMMAND ${Vulkan_SLANGC} ${full_path} -o ${output_file} -entry main -target spirv ${slangc_args}
        DEPENDS ${full_path} ${shader_common_files}
        COMMENT "slangc ${file_name} -> ${file_name}.spv"
        VERBATIM
    )
endforeach()

# ============================================================
# Targets
# ============================================================
add_custom_target(gkNextShaders 
    DEPENDS ${all_shader_outputs}
    SOURCES ${shader_files} ${shader_common_files}
)

add_custom_target(Assets 
    DEPENDS ${all_asset_stamps}
    SOURCES ${all_asset_files}
)

add_dependencies(Assets gkNextShaders)
set_target_properties(gkNextShaders PROPERTIES FOLDER "Shaders")
