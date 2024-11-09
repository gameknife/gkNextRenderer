
file(GLOB font_files fonts/*.ttf)
file(GLOB locale_files locale/*.txt)
file(GLOB lego_files legos/*.mls)
file(GLOB sfx_files sfx/*.mp3 sfx/*.wav)
file(GLOB model_files models/*.glb models/*.obj models/*.mtl)
file(GLOB shader_files shaders/*.vert shaders/*.frag shaders/*.rgen shaders/*.rchit shaders/*.rint shaders/*.rmiss shaders/*.comp)
file(GLOB texture_files textures/*.jpg textures/*.png textures/*.txt textures/*.hdr)
file(GLOB shader_extra_files shaders/*.glsl)
file(GLOB shader_common_files shaders/common/*.glsl)
file(GLOB pak_files paks/*.pak)
set_source_files_properties(${shader_extra_files} PROPERTIES HEADER_FILE_ONLY TRUE)

set(output_base_dir ${CMAKE_CURRENT_BINARY_DIR})
if ( ANDROID )
set(output_base_dir ${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/assets/assets/)
endif()

macro(copy_assets asset_files dir_name copied_files)
	foreach(asset ${${asset_files}})
		#message("asset: ${asset}")
		get_filename_component(file_name ${asset} NAME)
		get_filename_component(full_path ${asset} ABSOLUTE)
		set(output_dir ${output_base_dir}/${dir_name})
		set(output_file ${output_dir}/${file_name})
		set(${copied_files} ${${copied_files}} ${output_file})
		set(${copied_files} ${${copied_files}} PARENT_SCOPE)
		set_source_files_properties(${asset} PROPERTIES HEADER_FILE_ONLY TRUE)
		if (CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
			add_custom_command(
				OUTPUT ${output_file}
				#COMMAND mklink \"${output_file}\" \"${full_path}\"
				COMMAND xcopy \"${full_path}\" \"${output_file}*\" /Y /Q /F
				DEPENDS ${full_path}
			)
		else()
			add_custom_command(
				OUTPUT ${output_file}
				COMMAND mkdir -p ${output_dir} && cp -f -L \"${full_path}\" \"${output_file}\"
				DEPENDS ${full_path}
			)
		endif()
	endforeach()
endmacro()

copy_assets(font_files fonts copied_fonts)
copy_assets(locale_files locale copied_locale)
copy_assets(lego_files legos copied_legos)
copy_assets(sfx_files sfx copied_sfx)
copy_assets(model_files models copied_models)
copy_assets(texture_files textures copied_textures)
copy_assets(pak_files paks copied_paks)

source_group("Locales" FILES ${locale_files})
source_group("Fonts" FILES ${font_files})
source_group("Models" FILES ${model_files})
source_group("Shaders" FILES ${shader_files} ${shader_extra_files})
source_group("Shaders.Common" FILES ${shader_common_files})
source_group("Textures" FILES ${texture_files})
source_group("Paks" FILES ${pak_files})

add_custom_target(
	Assets 
	DEPENDS ${copied_paks} ${copied_fonts} ${copied_legos} ${copied_sfx} ${copied_locale} ${copied_models} ${compiled_shaders} ${copied_textures} 
	SOURCES ${pak_files} ${font_files} ${lego_files} ${sfx_files} ${locale_files} ${model_files} ${shader_files} ${shader_extra_files} ${texture_files} ${shader_common_files})


# Shader compilation -> POST_BUILD
foreach(shader ${shader_files})
	#message("SHADER: ${shader}")
	get_filename_component(file_name ${shader} NAME)
	get_filename_component(full_path ${shader} ABSOLUTE)
	set(output_dir ${output_base_dir}/shaders)
	set(output_file ${output_dir}/${file_name}.spv)
	set(compiled_shaders ${compiled_shaders} ${output_file})
	set(compiled_shaders ${compiled_shaders} PARENT_SCOPE)
	set_source_files_properties(${shader} PROPERTIES HEADER_FILE_ONLY TRUE)
        add_custom_command(
            TARGET Assets POST_BUILD
			COMMAND echo \"compiling shader: ${file_name} -> ${file_name}.spv\"
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${Vulkan_GLSLANG_VALIDATOR} --quiet --target-env vulkan1.2 -V ${full_path} -o ${output_file}
			DEPENDS ${full_path}
        )
endforeach()

# Shader Preprocessor -> PRE_BUILDZ
if (CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
if ( ANDROID )
add_custom_command(
    TARGET Assets PRE_BUILD
	COMMAND copy /y \"${CMAKE_CURRENT_LIST_DIR}\\shaders\\common\\Platform_MOBILE.glsl\" \"${CMAKE_CURRENT_LIST_DIR}\\shaders\\Platform.glsl\"
)
else()
add_custom_command(
    TARGET Assets PRE_BUILD
    COMMAND copy /y \"${CMAKE_CURRENT_LIST_DIR}\\shaders\\common\\Platform_DESKTOP.glsl\" \"${CMAKE_CURRENT_LIST_DIR}\\shaders\\Platform.glsl\"
)
endif()
else()
if ( ANDROID )
add_custom_command(
	TARGET Assets PRE_BUILD
	COMMAND cp -f -L \"${CMAKE_CURRENT_LIST_DIR}/shaders/common/Platform_MOBILE.glsl\" \"${CMAKE_CURRENT_LIST_DIR}/shaders/Platform.glsl\"
)
else()
add_custom_command(
	TARGET Assets PRE_BUILD
	COMMAND cp -f -L \"${CMAKE_CURRENT_LIST_DIR}/shaders/common/Platform_DESKTOP.glsl\" \"${CMAKE_CURRENT_LIST_DIR}/shaders/Platform.glsl\"
)
endif()
endif()