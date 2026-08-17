option(GK_BUILD_SHADER_TESTS "Build debug/test-only Slang shader entries" OFF)

file(GLOB shader_source_files CONFIGURE_DEPENDS
    shaders/*.comp.slang
    shaders/*.vert.slang
    shaders/*.frag.slang
)
set(shader_files ${shader_source_files})
set(shader_test_files
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Remote.BgraToYuv.comp.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Util.SharcCompileTest.comp.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Util.Bindless3DCompileTest.comp.slang"
)
if(NOT GK_BUILD_SHADER_TESTS)
    list(REMOVE_ITEM shader_files ${shader_test_files})
endif()

file(GLOB_RECURSE shader_common_files CONFIGURE_DEPENDS
    shaders/Shader/*.slang
    shaders/common/*.h
    shaders/common/*.slang
    shaders/third_party/*.h
)
source_group("Shaders" FILES ${shader_source_files} ${shader_common_files})

execute_process(
    COMMAND ${Vulkan_SLANGC} -v
    OUTPUT_VARIABLE slangc_version
    ERROR_VARIABLE slangc_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT slangc_version)
    set(slangc_version ${slangc_version_error})
endif()
message(STATUS "slangc compiler version: ${slangc_version}")

function(gk_compile_slang_shader shader outputVar)
    get_filename_component(file_name ${shader} NAME)
    get_filename_component(full_path ${shader} ABSOLUTE)
    set(output_dir ${output_base_dir}/shaders)
    set(output_file ${output_dir}/${file_name}.spv)
    set(dep_file ${output_file}.d)

    set_source_files_properties(${shader} PROPERTIES
        HEADER_FILE_ONLY TRUE
        VS_TOOL_OVERRIDE "None"
    )

    set(slangc_args "")
    if(WIN32 AND GK_ENABLE_SHADER_CLOCK)
        list(APPEND slangc_args -DSHADER_CLOCK)
    endif()
    if(APPLE)
        list(APPEND slangc_args -DPLATFORM_APPLE)
    endif()
    if(ANDROID)
        list(APPEND slangc_args -DPLATFORM_ANDROID)
    endif()

    if(file_name STREQUAL "Core.SharcUpdate.comp.slang")
        list(APPEND slangc_args -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=1 -DSHARC_QUERY=0)
    elseif(file_name STREQUAL "Core.SharcQuery.comp.slang")
        list(APPEND slangc_args -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=0 -DSHARC_QUERY=1)
    elseif(file_name STREQUAL "Core.SharcResolve.comp.slang")
        list(APPEND slangc_args -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=0 -DSHARC_QUERY=0)
    elseif(file_name STREQUAL "Util.SharcCompileTest.comp.slang")
        list(APPEND slangc_args -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=1 -DSHARC_QUERY=1)
    elseif(file_name MATCHES "^Core\\.Sharc")
        list(APPEND slangc_args -DGK_ENABLE_OFFICIAL_SHARC)
    endif()

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
        COMMAND ${Vulkan_SLANGC}
            ${full_path}
            -o ${output_file}
            -entry main
            -target spirv
            -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            -depfile ${dep_file}
            ${slangc_args}
        DEPENDS ${full_path}
        DEPFILE ${dep_file}
        COMMENT "slangc ${file_name} -> ${file_name}.spv"
        VERBATIM
    )

    set(${outputVar} ${output_file} PARENT_SCOPE)
endfunction()

set(all_shader_outputs "")
foreach(shader IN LISTS shader_files)
    gk_compile_slang_shader(${shader} shader_output)
    list(APPEND all_shader_outputs ${shader_output})
endforeach()

if(ANDROID)
    # Shader outputs live in the Android asset tree. Remove outputs whose source
    # disappeared so a deleted shader cannot remain loadable from an old APK.
    set(expected_shader_files ${all_shader_outputs})
    foreach(shader_output IN LISTS all_shader_outputs)
        list(APPEND expected_shader_files "${shader_output}.d")
    endforeach()
    file(GLOB generated_shader_files "${output_base_dir}/shaders/*")
    foreach(generated_shader IN LISTS generated_shader_files)
        list(FIND expected_shader_files "${generated_shader}" shader_output_index)
        if(shader_output_index EQUAL -1)
            file(REMOVE "${generated_shader}")
        endif()
    endforeach()
endif()
