set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# directory naming
file(GLOB_RECURSE src_files_assets "Assets/*.cpp" "Assets/*.hpp")
file(GLOB_RECURSE src_files_utilities "Utilities/*.cpp" "Utilities/*.hpp")
file(GLOB_RECURSE src_files_vulkan "Vulkan/*.cpp" "Vulkan/*.hpp")
file(GLOB_RECURSE src_files_thirdparty 
	"ThirdParty/json11/*.cpp" 
	"ThirdParty/json11/*.hpp" 
	"ThirdParty/mikktspace/*.c" 
	"ThirdParty/mikktspace/*.h"
	"ThirdParty/miniaudio/*.h"
	"ThirdParty/lzav/*.h"
	"ThirdParty/tinybvh/*.h"
	)
if (MSVC)
set_source_files_properties(
  ${src_files_thirdparty}
  PROPERTIES
  COMPILE_FLAGS "/W0"
) 
else()
set_source_files_properties(
  ${src_files_thirdparty}
  PROPERTIES
  COMPILE_FLAGS "-w"
) 
endif()
file(GLOB_RECURSE src_files_engine
	"Common/*.hpp" 
	"Runtime/*.h"
	"Runtime/*.hpp"
	"Runtime/*.cpp"
	"Options.cpp"
	"Options.hpp"
)
file(GLOB_RECURSE src_files_editor "Editor/*")
file(GLOB_RECURSE src_files_magicalego "MagicaLego/*.cpp" "MagicaLego/*.hpp")
file(GLOB_RECURSE src_files_gkrenderer "Application/gkNextRenderer/*")
file(GLOB_RECURSE src_files_gkbenchmark "Application/gkNextBenchmark/*")

# libray and app
if ( ANDROID )
add_library(gkNextEngine
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_engine}
	${src_files_vulkan} 
	${src_files_thirdparty}
)
add_library(gkNextRenderer SHARED
	${src_files_gkrenderer}
	AndroidMain.cpp
	GameActivitySources.cpp
)
else()
add_library(gkNextEngine
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_engine}
	${src_files_vulkan} 
	${src_files_thirdparty}
)
add_executable(gkNextRenderer
	${src_files_gkrenderer}
	DesktopMain.cpp
)
add_executable(gkNextBenchmark
	${src_files_gkbenchmark}
	DesktopMain.cpp
)
add_executable(gkNextEditor
	${src_files_editor}
	DesktopMain.cpp
)
add_executable(MagicaLego
	${src_files_magicalego}
	DesktopMain.cpp
)
add_executable(Packager
	${src_files_utilities} 
	Packager/PackagerMain.cpp
)
endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	# GCC8 needs an extra lib for <filesystem>.
	# This is not needed with GCC9 or higher.
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

# editor stuff
add_subdirectory(ThirdParty/ImNodeFlow)
add_subdirectory(ThirdParty/quickjs-ng)
# postprocess: target setup
if ( ANDROID )
set(AllTargets 
gkNextEngine
gkNextRenderer
)
else()
set(AllTargets 
gkNextEngine
gkNextRenderer
gkNextBenchmark
gkNextEditor
Packager
MagicaLego 
)
endif()

# common setup
foreach(target IN LISTS AllTargets)
	# common dependencies
	add_dependencies(${target} Assets)
	set_target_properties(${target} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})

	# include paths
	target_include_directories(${target} PRIVATE .)
	target_include_directories(${target} PRIVATE ${STB_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${Vulkan_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${CPP_BASE64_INCLUDE_DIRS})

	# preprocessor definitions
	target_compile_definitions(${target} PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
	target_compile_definitions(${target} PUBLIC MA_NO_ENCODING MA_NO_FLAC)
	# if debug
	# 假设你的目标名称是 MyTarget
	target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:JPH_DEBUG_RENDERER JPH_PROFILE_ENABLED>)
	
	# link engine
	if ( ${target} STREQUAL gkNextEngine )
		target_compile_definitions(${target} PRIVATE ENGINE_EXPORTS)
		target_link_libraries(${target} PRIVATE cxxopts::cxxopts Jolt::Jolt quickjs KTX::ktx fmt::fmt CURL::libcurl glm::glm imgui::imgui tinyobjloader::tinyobjloader draco::draco ${Vulkan_LIBRARIES} ${extra_libs})
		if ( NOT ANDROID ) 
		target_link_libraries(${target} PRIVATE glfw)
		target_link_libraries(${target} PRIVATE cpptrace::cpptrace)
		endif()
	else()
		set_target_properties(${target} PROPERTIES FOLDER "Programs")
		target_link_libraries(${target} PRIVATE gkNextEngine fmt::fmt)
	endif()

	# for android
	if ( ANDROID)
		target_include_directories(${target} PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
		target_link_libraries(${target} PRIVATE game-activity::game-activity log android)
	endif()

	if (CMAKE_CXX_COMPILER MATCHES ".*mingw.*")
		target_link_libraries(${target} PRIVATE gdi32)
	endif()

	if ( WITH_AVIF )
		target_compile_definitions(${target} PUBLIC WITH_AVIF=1)
		target_compile_definitions(${target} PUBLIC AVIF_CODEC_AOM=SYSTEM)
		target_link_libraries(${target} PRIVATE avif)
	endif()

	if( WITH_SUPERLUMINAL )
		target_compile_definitions(${target} PUBLIC WITH_SUPERLUMINAL=1)
		target_include_directories(${target} SYSTEM PRIVATE ${SuperluminalAPI_INCLUDE_DIRS})
		target_link_libraries(${target} PRIVATE SuperluminalAPI)
	endif()

	if ( WITH_OIDN )
		target_compile_definitions(${target} PUBLIC WITH_OIDN=1)
		target_include_directories(${target} PRIVATE ../src/ThirdParty/oidn/include/)
		target_link_directories(${target} PRIVATE ../src/ThirdParty/oidn/lib/)
		target_link_libraries(${target} PRIVATE OpenImageDenoise OpenImageDenoise_core )
	endif()

	if( WIN32 )
		target_compile_definitions(${target} PUBLIC VK_USE_PLATFORM_WIN32_KHR PLATFORM__WINDOWS)
	endif()	

	# warning as error
	if (MSVC)
		target_compile_options(${target} PRIVATE /WX)
		target_compile_options(${target} PRIVATE /wd4200)
	else()
	target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-but-set-variable -Wno-inconsistent-missing-override -Wno-unused-const-variable -Wno-unused-variable -Wno-unused-private-field -Wno-unused-function -Wno-sign-compare -Wno-unused-result)
		if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			target_compile_options(${target} PRIVATE -Wno-stringop-overflow)
		endif()
	endif()

	# specific for targets
	if (UNIX AND NOT APPLE AND NOT ANDROID)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -mavx")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mavx")
	endif()

	if ( ${target} STREQUAL MagicaLego )
	if (MSVC)
	add_custom_command(
			TARGET ${target} POST_BUILD
			COMMAND echo \"copy ffmpeg exe...\"
			COMMAND xcopy \"${CMAKE_CURRENT_SOURCE_DIR}/../src/ThirdParty/ffmpeg/bin/ffmpeg.exe\" \"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\" /Y /Q /F
		)
	endif()
	endif()

	if ( ${target} STREQUAL gkNextEditor )
	target_link_libraries(gkNextEditor PRIVATE ImNodeFlow)
	set_target_properties(ImNodeFlow PROPERTIES FOLDER "ThirdParty")
	# third-party warning fix
	if (MSVC)

	else()
	set_source_files_properties(${src_files_editor} PROPERTIES COMPILE_FLAGS "-w")
	endif()
	endif()

endforeach()

set_target_properties(quickjs PROPERTIES FOLDER "ThirdParty")
set_target_properties(Assets PROPERTIES FOLDER "Engine")
set_target_properties(gkNextEngine PROPERTIES FOLDER "Engine")