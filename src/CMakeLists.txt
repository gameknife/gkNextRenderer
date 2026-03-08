set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Include explicit source file lists (replaces GLOB_RECURSE for better incremental builds)
include(cmake/SourceFiles.cmake)

# Disable warnings for third-party code
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

if (IOS)
    set_source_files_properties(Runtime/Subsystems/NextAudio.cpp PROPERTIES LANGUAGE "OBJCXX")
endif()

# unit test files
set(UNIT_TEST_SOURCES
	Tests/TestCommon.cpp
	Tests/Test_PhysicsSync.cpp
	Tests/Test_GpuTimerLogic.cpp
	Tests/Test_GltfSkinning.cpp
	Tests/Test_ComponentSystem.cpp
	Tests/Test_LDrawConfig.cpp
	Tests/Test_LDrawGeometry.cpp
	Tests/Test_LDrawParser.cpp
	Tests/Test_RenderComponent.cpp
	Tests/Test_ImAnimIntegration.cpp
	Tests/Test_MagicaLegoStyle.cpp
	Tests/Test_MagicaLegoScript.cpp
	Tests/Test_MagicaLegoPlacementRules.cpp
	Application/MagicaLego/MagicaLegoStyle.cpp
	Application/MagicaLego/MagicaLegoScriptParser.cpp
	Application/MagicaLego/MagicaLegoPlacementRules.cpp
)

if (ENABLE_PHYSIC)
	list(APPEND UNIT_TEST_SOURCES Tests/Test_PhysicsComponent.cpp)
endif()


# libray and app
if ( ANDROID )
add_library(gkNextRenderer SHARED
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_engine}
	${src_files_vulkan} 
	${src_files_rendering}
	${src_files_thirdparty}
	${src_files_customimgui}
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
add_executable(gkNextVisualTest
	${src_files_gkvisualtest}
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

if( NOT MINGW )
find_package(Catch2 3 REQUIRED)
add_executable(gkNextUnitTests ${UNIT_TEST_SOURCES})
endif()

endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

if ( WITH_OZZ )
add_subdirectory(ThirdParty/ozz)
target_link_libraries(ozz PRIVATE gk_project_options)
endif()

add_subdirectory(ThirdParty/ImNodeFlow)
target_link_libraries(ImNodeFlow PRIVATE gk_project_options)

if( WITH_QUICKJS)
add_subdirectory(ThirdParty/quickjs-ng)
target_link_libraries(quickjs PRIVATE gk_project_options)
endif()

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
elseif( MINGW )
set(AllTargets
gkNextEngine
gkNextRenderer
gkNextStillBenchmark
gkNextMotionBenchmark
gkNextVisualTest
gkNextEditor
Packager
MagicaLego 
)
else()
set(AllTargets 
gkNextEngine
gkNextRenderer
gkNextStillBenchmark
gkNextMotionBenchmark
gkNextVisualTest
gkNextEditor
Packager
MagicaLego 
gkNextUnitTests
)
endif()

# Unity Build option (default ON, can be disabled with -DENABLE_UNITY_BUILD=OFF)
option(ENABLE_UNITY_BUILD "Enable unity builds for engine target" ON)

# common setup
foreach(target IN LISTS AllTargets)
	# common dependencies
	add_dependencies(${target} Assets)
	target_link_libraries(${target} PRIVATE gk_project_options)

	set_target_properties(${target} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})

	# include paths
	target_include_directories(${target} PRIVATE .)
	target_include_directories(${target} PRIVATE ${STB_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${Vulkan_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${CPP_BASE64_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ThirdParty/ozz/include)
	target_include_directories(${target} PRIVATE ThirdParty/ImAnim)

	# preprocessor definitions
	target_compile_definitions(${target} PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
	target_compile_definitions(${target} PUBLIC MA_NO_ENCODING MA_NO_FLAC)

	if( IOS )
	target_compile_definitions(${target} PUBLIC IOS=1)
	endif()

	# link engine
	if ( ${target} STREQUAL gkNextEngine )
		target_compile_definitions(${target} PRIVATE ENGINE_EXPORTS)
		target_compile_definitions(${target} PRIVATE GK_NEXT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
		
		# Enable Unity Build for gkNextEngine unless explicitly disabled (Android disabled by default)
		# Group files for Unity Build optimization
		if ( ENABLE_UNITY_BUILD AND NOT ANDROID )
		set_target_properties(${target} PROPERTIES UNITY_BUILD ON)
		set_target_properties(${target} PROPERTIES UNITY_BUILD_BATCH_SIZE 12)

		set_source_files_properties(${src_files_assets} PROPERTIES UNITY_GROUP "assets")
		set_source_files_properties(${src_files_utilities} PROPERTIES UNITY_GROUP "utilities")
		set_source_files_properties(${src_files_engine} PROPERTIES UNITY_GROUP "engine")
		set_source_files_properties(${src_files_vulkan} PROPERTIES UNITY_GROUP "vulkan")
		set_source_files_properties(${src_files_thirdparty} PROPERTIES UNITY_GROUP "thirdparty")

		# Exclude files with STB implementation from Unity Build to avoid redefinition conflicts
		set_source_files_properties(Assets/Loaders/FSceneLoader.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		set_source_files_properties(Assets/Savers/FSceneSaver.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		endif()
		find_package(WebP CONFIG REQUIRED)
		target_link_libraries(${target} PRIVATE SDL3::SDL3 xxHash::xxhash meshoptimizer::meshoptimizer fmt::fmt CURL::libcurl glm::glm imgui::imgui draco::draco WebP::webp ${extra_libs})
		target_link_libraries(${target} PUBLIC spdlog::spdlog)
		if ( WITH_WHISPERCPP )
			find_package(whisper CONFIG REQUIRED)
			target_link_libraries(${target} PRIVATE whisper)
		endif()
		
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

		if ( WITH_STREAMLINE )
			target_compile_definitions(${target} PUBLIC WITH_STREAMLINE=1)
			target_include_directories(${target} PRIVATE ${STREAMLINE_INCLUDE_DIR})
			target_link_directories(${target} PRIVATE ${STREAMLINE_LIB_DIR})
			target_link_libraries(${target} PRIVATE sl.interposer)
		endif()
	else()
		set_target_properties(${target} PROPERTIES FOLDER "Programs")
		if ( NOT ANDROID ) 
		target_link_libraries(${target} PRIVATE gkNextEngine)
		endif()
		if ( WITH_STREAMLINE ) 
		target_link_directories(${target} PRIVATE ${STREAMLINE_LIB_DIR})
		target_link_libraries(${target} PRIVATE sl.interposer)
		endif()
	endif()

	# for android
	if ( ANDROID )
		find_package(WebP CONFIG REQUIRED)
		target_link_options(${target} PRIVATE
	    -Wl,-z,max-page-size=16384
		-Wl,-z,common-page-size=16384
		-Wl,--hash-style=gnu
		)
		target_link_libraries(${target} PRIVATE android SDL3::SDL3 spdlog::spdlog xxHash::xxhash meshoptimizer::meshoptimizer fmt::fmt CURL::libcurl glm::glm imgui::imgui draco::draco WebP::webp ${Vulkan_LIBRARIES} ${extra_libs})
	endif()

	if ( MINGW )
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
		target_include_directories(${target} PRIVATE ${OIDN_INCLUDE_DIR})
		target_link_directories(${target} PRIVATE ${OIDN_LIB_DIR})
		target_link_libraries(${target} PRIVATE OpenImageDenoise )
	endif()

	if ( WITH_PHYSIC )
		target_compile_definitions(${target} PUBLIC WITH_PHYSIC=1)
		target_link_libraries(${target} PRIVATE Jolt::Jolt )
	endif()

	if ( WITH_AUDIO )
		target_compile_definitions(${target} PUBLIC WITH_AUDIO=1)
	endif()

	if ( WITH_QUICKJS )
		target_compile_definitions(${target} PUBLIC WITH_QUICKJS=1)
		target_link_libraries(${target} PRIVATE quickjs )
	endif()

	if ( WITH_WHISPERCPP )
		target_compile_definitions(${target} PUBLIC WITH_WHISPERCPP=1)
	endif()

	if ( WITH_OZZ )
		target_compile_definitions(${target} PUBLIC WITH_OZZ=1)
		target_link_libraries(${target} PRIVATE ozz )
	endif()

	if ( WITH_KTX2 )
		target_compile_definitions(${target} PUBLIC WITH_KTX2=1)
		target_link_libraries(${target} PRIVATE KTX::ktx )
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
	if ( (UNIX OR MINGW) AND NOT APPLE AND NOT ANDROID)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -mavx")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mavx")
	endif()

	if ( ${target} STREQUAL MagicaLego )
	if (MSVC)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../src/ThirdParty/ffmpeg/bin/ffmpeg.exe DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
	endif()
	endif()

	# copy the tsc to bin
	if ( ${target} STREQUAL gkNextEngine )
	if ( WITH_QUICKJS AND EXISTS "${TSC_EXECUTABLE}" )
		file(COPY "${TSC_EXECUTABLE}" DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
	endif()

	if ( WITH_STREAMLINE )
		file(GLOB SL_BINS "${STREAMLINE_BIN_DIR}/*")
		file(COPY ${SL_BINS} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
	endif()

	if ( WITH_OIDN )
		file(GLOB OIDN_BINS "${OIDN_BIN_DIR}/*")
		file(COPY ${OIDN_BINS} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
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

# Test Target
if ( NOT ANDROID AND NOT IOS AND NOT MINGW )
target_link_libraries(gkNextUnitTests PRIVATE gkNextEngine Catch2::Catch2WithMain gk_project_options)
set_target_properties(gkNextUnitTests PROPERTIES FOLDER "Tests")
endif()



set_target_properties(ImNodeFlow PROPERTIES FOLDER "ThirdParty")

if ( WITH_QUICKJS )
set_target_properties(quickjs PROPERTIES FOLDER "ThirdParty")
endif()

if ( WITH_OZZ )
set_target_properties(ozz PROPERTIES FOLDER "ThirdParty")
endif()
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
