set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# directory naming
file(GLOB_RECURSE src_files_assets "Assets/*.cpp" "Assets/*.hpp" "Assets/*.h" "../assets/shaders/common/*.h")
file(GLOB_RECURSE src_files_utilities "Utilities/*.cpp" "Utilities/*.hpp" "Utilities/*.h")
file(GLOB_RECURSE src_files_vulkan "Vulkan/*.cpp" "Vulkan/*.hpp")
file(GLOB_RECURSE src_files_rendering "Rendering/*.cpp" "Rendering/*.hpp")
file(GLOB_RECURSE src_files_thirdparty 
	"ThirdParty/json11/*.cpp" 
	"ThirdParty/json11/*.hpp" 
	"ThirdParty/mikktspace/*.c" 
	"ThirdParty/mikktspace/*.h"
	"ThirdParty/miniaudio/*.h"
	"ThirdParty/lzav/*.h"
	"ThirdParty/tinybvh/*.h"
	"ThirdParty/streamline/*.h"
	"ThirdParty/ozz/*.h"
	"ThirdParty/imgui-custom/*.cpp"
	"ThirdParty/imgui-custom/*.h"
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

# Set Engine.cpp to compile as Objective-C++ on iOS
if (IOS)
    set_source_files_properties(Runtime/Engine.cpp PROPERTIES LANGUAGE "OBJCXX")
endif()
file(GLOB_RECURSE src_files_editor "Editor/*")
file(GLOB_RECURSE src_files_magicalego "Application/MagicaLego/*.cpp" "MagicaLego/*.hpp")
file(GLOB_RECURSE src_files_gkrenderer "Application/gkNextRenderer/*")
file(GLOB_RECURSE src_files_benchmarkcommon "Application/gkNextBenchmark/Common/*")
file(GLOB_RECURSE src_files_gkstillbenchmark "Application/gkNextBenchmark/gkNextStillBenchmark/*")
file(GLOB_RECURSE src_files_gkmotionbenchmark "Application/gkNextBenchmark/gkNextMotionBenchmark/*")

# libray and app
if ( ANDROID )
add_library(gkNextRenderer SHARED
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_engine}
	${src_files_vulkan} 
	${src_files_rendering}
	${src_files_thirdparty}
	${src_files_gkrenderer}
	AndroidMain.cpp
	#GameActivitySources.cpp
)
elseif( IOS )
add_library(gkNextEngine
	${src_files_assets}
	${src_files_utilities}
	${src_files_engine}
	${src_files_vulkan}
	${src_files_rendering}
	${src_files_thirdparty}
)
add_executable(gkNextRenderer
	MACOSX_BUNDLE
	${src_files_gkrenderer}
	DesktopMain.cpp
)
else()
add_library(gkNextEngine
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_engine}
	${src_files_vulkan} 
	${src_files_rendering}
	${src_files_thirdparty}
)
add_executable(gkNextRenderer
	${src_files_gkrenderer}
	DesktopMain.cpp
)
add_executable(gkNextStillBenchmark
	${src_files_benchmarkcommon}
	${src_files_gkstillbenchmark}
	DesktopMain.cpp
)
add_executable(gkNextMotionBenchmark
	${src_files_benchmarkcommon}
	${src_files_gkmotionbenchmark}
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
	Application/Packager/PackagerMain.cpp
)
endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

# editor stuff
add_subdirectory(ThirdParty/ozz)
add_subdirectory(ThirdParty/ImNodeFlow)
add_subdirectory(ThirdParty/quickjs-ng)
# postprocess: target setup
if ( ANDROID )
set(AllTargets
gkNextRenderer
)
elseif( IOS )
set(AllTargets
gkNextEngine
gkNextRenderer
)
else()
set(AllTargets 
gkNextEngine
gkNextRenderer
gkNextStillBenchmark
gkNextMotionBenchmark
gkNextEditor
Packager
MagicaLego 
)
endif()

# 是否启用 Unity Build（默认开启以维持原有行为，可在配置时通过 -DGK_ENABLE_UNITY_BUILD=OFF 关闭）
option(GK_ENABLE_UNITY_BUILD "Enable unity builds for engine target" ON)

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
	target_include_directories(${target} PRIVATE ThirdParty/ozz/include)

	# preprocessor definitions
	target_compile_definitions(${target} PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
	target_compile_definitions(${target} PUBLIC MA_NO_ENCODING MA_NO_FLAC)

	if( IOS )
	target_compile_definitions(${target} PUBLIC IOS=1)
	endif()

	# link engine
	if ( ${target} STREQUAL gkNextEngine )
		target_compile_definitions(${target} PRIVATE ENGINE_EXPORTS)
		
		# 若未显式关闭则为 gkNextEngine 启用 Unity Build（Android 默认仍禁用以保持行为）
		# Group files for Unity Build optimization
		if ( GK_ENABLE_UNITY_BUILD AND NOT ANDROID ) 
		set_target_properties(${target} PROPERTIES UNITY_BUILD ON)
		set_target_properties(${target} PROPERTIES UNITY_BUILD_BATCH_SIZE 6)

		set_source_files_properties(${src_files_assets} PROPERTIES UNITY_GROUP "assets")
		set_source_files_properties(${src_files_utilities} PROPERTIES UNITY_GROUP "utilities") 
		set_source_files_properties(${src_files_engine} PROPERTIES UNITY_GROUP "engine")
		set_source_files_properties(${src_files_vulkan} PROPERTIES UNITY_GROUP "vulkan")
		set_source_files_properties(${src_files_thirdparty} PROPERTIES UNITY_GROUP "thirdparty")
		endif()

		target_link_libraries(${target} PRIVATE SDL3::SDL3 spdlog::spdlog ozz xxHash::xxhash meshoptimizer::meshoptimizer Jolt::Jolt quickjs KTX::ktx fmt::fmt CURL::libcurl glm::glm imgui::imgui tinyobjloader::tinyobjloader draco::draco ${extra_libs})
		
		# handle moltenvk for iOS
		if ( IOS )
		get_target_property(_ifaces imgui::imgui INTERFACE_LINK_LIBRARIES)
		list(REMOVE_ITEM _ifaces Vulkan::Vulkan)
		set_target_properties(imgui::imgui PROPERTIES INTERFACE_LINK_LIBRARIES "${_ifaces}")
		target_link_libraries(${target} PRIVATE ${MOLTENVK_LIBRARY})
		else()
		target_link_libraries(${target} PRIVATE ${Vulkan_LIBRARIES})
		endif()

		if ( NOT ANDROID AND NOT IOS )
		target_link_libraries(${target} PRIVATE cpptrace::cpptrace)
		endif()
	else()
		set_target_properties(${target} PROPERTIES FOLDER "Programs")
		if ( NOT ANDROID ) 
		target_link_libraries(${target} PRIVATE gkNextEngine)
		endif()
		if ( WITH_STREAMLINE ) 
		target_link_directories(${target} PRIVATE ThirdParty/streamline/lib/x64)
		target_link_libraries(${target} PRIVATE sl.interposer)
		endif()
	endif()

	# for android
	if ( ANDROID )
		target_link_options(${target} PRIVATE
	    -Wl,-z,max-page-size=16384
		-Wl,-z,common-page-size=16384
		-Wl,--hash-style=gnu
		)
		target_link_libraries(${target} PRIVATE android SDL3::SDL3 spdlog::spdlog ozz xxHash::xxhash meshoptimizer::meshoptimizer Jolt::Jolt quickjs KTX::ktx fmt::fmt CURL::libcurl glm::glm imgui::imgui tinyobjloader::tinyobjloader draco::draco ${Vulkan_LIBRARIES} ${extra_libs})
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
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../src/ThirdParty/ffmpeg/bin/ffmpeg.exe DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
	endif()
	endif()

	if ( ${target} STREQUAL gkNextEditor )
	target_link_libraries(gkNextEditor PRIVATE ImNodeFlow)
	# third-party warning fix
	if (MSVC)

	else()
	set_source_files_properties(${src_files_editor} PROPERTIES COMPILE_FLAGS "-w")
	endif()
	endif()

endforeach()

set_target_properties(ImNodeFlow PROPERTIES FOLDER "ThirdParty")
set_target_properties(quickjs PROPERTIES FOLDER "ThirdParty")
set_target_properties(ozz PROPERTIES FOLDER "ThirdParty")
set_target_properties(Assets PROPERTIES FOLDER "Engine")
if ( NOT ANDROID )
set_target_properties(gkNextEngine PROPERTIES FOLDER "Engine")
endif()

# iOS specific: copy assets to bundle
if ( IOS )
    set_target_properties(gkNextRenderer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../ios/Info.plist
    )

    # Add custom command to copy assets to iOS bundle
    add_custom_command(TARGET gkNextRenderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/../assets
            $<TARGET_BUNDLE_DIR:gkNextRenderer>
        COMMENT "Copying assets to iOS bundle"
    )
endif()
