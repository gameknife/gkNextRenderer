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
    set_source_files_properties(Engine/Runtime/Subsystems/NextAudio.cpp PROPERTIES LANGUAGE "OBJCXX")
endif()

# unit test files
set(UNIT_TEST_SOURCES
	Tests/TestCommon.cpp
	Tests/Test_PhysicsSync.cpp
	Tests/Test_GpuTimerLogic.cpp
	Tests/Test_GltfSkinning.cpp
	Tests/Test_CameraFocus.cpp
	Tests/Test_ComponentSystem.cpp
	Tests/Test_GameplayComponents.cpp
	Tests/Test_LDrawConfig.cpp
	Tests/Test_LDrawGeometry.cpp
	Tests/Test_LDrawLoader.cpp
	Tests/Test_LDrawParser.cpp
	Tests/Test_LDrawConnectivity.cpp
	Tests/Test_CVarSystem.cpp
	Tests/Test_SceneList.cpp
	Tests/Test_ScadLoader.cpp
	Tests/Test_RenderComponent.cpp
	Tests/Test_Brotato3DPcg.cpp
	Tests/Test_ImAnimIntegration.cpp
	Tests/Test_BrickPlayerSnapLogic.cpp
	Application/Game/BrickPlayer/BrickPlayerSnapLogic.cpp
	Tests/Test_MagicaLegoStyle.cpp
	Tests/Test_MagicaLegoScript.cpp
	Tests/Test_MagicaLegoPlacementRules.cpp
	Application/Game/MagicaLego/MagicaLegoStyle.cpp
	Application/Game/MagicaLego/MagicaLegoScriptParser.cpp
	Application/Game/MagicaLego/MagicaLegoPlacementRules.cpp
	Tests/Test_FlappyRng.cpp
	Application/Game/Brotato3D/Brotato3DPcgGenerator.cpp
	Tests/Test_AIChatProtocol.cpp
	Tests/Test_LlamaPidFile.cpp
	Tests/Test_AgentLoop.cpp
	Tests/Test_RepoTools.cpp
	Tests/Test_ScadPromptContext.cpp
	Tests/Test_ScadSessionStore.cpp
	Application/Editor/ScadStudio/ScadPromptContext.cpp
	Application/Editor/ScadStudio/ScadSessionStore.cpp
)

list(APPEND UNIT_TEST_SOURCES Tests/Test_PhysicsComponent.cpp)


# libray and app
if ( ANDROID )
add_library(gkNextRenderer SHARED
	${src_files_assets}
	${src_files_utilities}
	${src_files_engine}
	${src_files_vulkan}
	${src_files_rendering}
	${src_files_thirdparty}
	${src_files_modules_all}
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
add_library(NextGameplay
	${src_files_nextgameplay}
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
add_library(NextGameplay
	${src_files_nextgameplay}
)
add_executable(gkNextRenderer
	${src_files_gkrenderer}
	DesktopMain.cpp
)
target_include_directories(gkNextRenderer PRIVATE Application/Render/gkNextRenderer)
add_executable(gkNextStillBenchmark
	${src_files_benchmarkcommon}
	${src_files_gkstillbenchmark}
	DesktopMain.cpp
)
target_include_directories(gkNextStillBenchmark PRIVATE Application/Render/gkNextBenchmark)
add_executable(gkNextMotionBenchmark
	${src_files_benchmarkcommon}
	${src_files_gkmotionbenchmark}
	DesktopMain.cpp
)
target_include_directories(gkNextMotionBenchmark PRIVATE Application/Render/gkNextBenchmark)
add_executable(gkNextVisualTest
	${src_files_gkvisualtest}
	DesktopMain.cpp
)
target_include_directories(gkNextVisualTest PRIVATE Application/Render/gkNextVisualTest)
add_executable(RmlUiDemo
	${src_files_rmluidemo}
	DesktopMain.cpp
)
target_include_directories(RmlUiDemo PRIVATE Application/Render/RmlUiDemo)
add_executable(gkNextEditor
	${src_files_editor}
	DesktopMain.cpp
)
target_include_directories(gkNextEditor PRIVATE Application/Editor/gkNextEditor)
add_executable(ScadStudio
	${src_files_scadstudio}
	DesktopMain.cpp
)
target_include_directories(ScadStudio PRIVATE Application/Editor/ScadStudio)
add_executable(MagicaLego
	${src_files_magicalego}
	DesktopMain.cpp
)
target_include_directories(MagicaLego PRIVATE Application/Game/MagicaLego)
add_executable(KongLie3D
	${src_files_konglie3d}
	DesktopMain.cpp
)
target_include_directories(KongLie3D PRIVATE Application/Game/KongLie3D)
add_executable(Brotato3D
	${src_files_brotato3d}
	DesktopMain.cpp
)
target_include_directories(Brotato3D PRIVATE Application/Game/Brotato3D)
target_compile_definitions(Brotato3D PRIVATE DEV_MODE=1)
add_executable(FlappyCpp
	${src_files_flappycpp}
	DesktopMain.cpp
)
target_include_directories(FlappyCpp PRIVATE Application/Game/Flappy)
add_executable(FlappyJs
	${src_files_flappyjs}
	DesktopMain.cpp
)
target_include_directories(FlappyJs PRIVATE Application/Game/Flappy)
add_executable(BrickPlayer
	${src_files_brickplayer}
	DesktopMain.cpp
)
target_include_directories(BrickPlayer PRIVATE Application/Game/BrickPlayer)
add_executable(CharacterDemo
	${src_files_characterdemo}
	DesktopMain.cpp
)
target_include_directories(CharacterDemo PRIVATE Application/Game/CharacterDemo)
add_executable(StudioSim
	${src_files_studiosim}
	DesktopMain.cpp
)
target_include_directories(StudioSim PRIVATE Application/Game/StudioSim)
add_executable(Packager
	Application/Util/Packager/PackagerMain.cpp
)

find_package(Catch2 3 REQUIRED)
add_executable(gkNextUnitTests ${UNIT_TEST_SOURCES})

endif()

# Optional engine modules: one static library per module directory that has
# sources (empty module skeletons are skipped). Android compiles module
# sources directly into the single SHARED target above.
set(GK_MODULE_TARGETS "")
if ( NOT ANDROID )
foreach(gk_module IN LISTS GK_MODULE_NAMES)
	if ( src_files_module_${gk_module} )
		add_library(${gk_module} STATIC ${src_files_module_${gk_module}})
		list(APPEND GK_MODULE_TARGETS ${gk_module})
	endif()
endforeach()
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

option(GK_WITH_REMOTE "Enable WebRTC remote play support" ON)
set(GK_REMOTE_ENABLED OFF)
set(GK_REMOTE_LINK_LIBS "")
if (GK_WITH_REMOTE AND NOT (ANDROID OR IOS OR APPLE))
    find_package(LibDataChannel CONFIG QUIET)
    find_package(httplib CONFIG QUIET)
    set(GK_REMOTE_LIBDATACHANNEL_TARGET "")
    if (TARGET LibDataChannel::LibDataChannel)
        set(GK_REMOTE_LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannel)
    elseif (TARGET LibDataChannel::LibDataChannelStatic)
        set(GK_REMOTE_LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannelStatic)
    endif()

    set(GK_OPENH264_TARGET "")
    find_path(GK_OPENH264_INCLUDE_DIR
        NAMES wels/codec_api.h
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        NO_DEFAULT_PATH)
    find_library(GK_OPENH264_LIBRARY_RELEASE
        NAMES openh264
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
        NO_DEFAULT_PATH)
    find_library(GK_OPENH264_LIBRARY_DEBUG
        NAMES openh264
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
        NO_DEFAULT_PATH)
    if (GK_OPENH264_INCLUDE_DIR AND GK_OPENH264_LIBRARY_RELEASE)
        add_library(gk_openh264 UNKNOWN IMPORTED)
        set_target_properties(gk_openh264 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${GK_OPENH264_INCLUDE_DIR}"
            IMPORTED_LOCATION_RELEASE "${GK_OPENH264_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${GK_OPENH264_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_MINSIZEREL "${GK_OPENH264_LIBRARY_RELEASE}")
        if (GK_OPENH264_LIBRARY_DEBUG)
            set_target_properties(gk_openh264 PROPERTIES IMPORTED_LOCATION_DEBUG "${GK_OPENH264_LIBRARY_DEBUG}")
        else()
            set_target_properties(gk_openh264 PROPERTIES IMPORTED_LOCATION_DEBUG "${GK_OPENH264_LIBRARY_RELEASE}")
        endif()
        set(GK_OPENH264_TARGET gk_openh264)
    endif()

    if (GK_REMOTE_LIBDATACHANNEL_TARGET AND TARGET httplib::httplib AND GK_OPENH264_TARGET)
        set(GK_REMOTE_ENABLED ON)
        list(APPEND GK_REMOTE_LINK_LIBS ${GK_REMOTE_LIBDATACHANNEL_TARGET} httplib::httplib ${GK_OPENH264_TARGET})
        message(STATUS "Remote Play enabled: ${GK_REMOTE_LIBDATACHANNEL_TARGET} + httplib::httplib + openh264")
    else()
        message(STATUS "Remote Play disabled: libdatachannel/cpp-httplib/openh264 targets not found")
    endif()
else()
    message(STATUS "Remote Play disabled for this platform or by GK_WITH_REMOTE=OFF")
endif()

set(GK_WHISPER_LINK_LIBS "")
set(GK_WHISPER_COMPILE_DEFINITIONS "")
if (WITH_WHISPERCPP)
	find_package(whisper CONFIG REQUIRED)
	if (MSVC)
		message(STATUS "Whisper.cpp is disabled for MSVC Debug builds due to upstream static runtime mismatch")
		set(GK_WHISPER_LINK_LIBS "$<$<NOT:$<CONFIG:Debug>>:whisper>")
		list(APPEND GK_WHISPER_COMPILE_DEFINITIONS
			"$<$<CONFIG:Debug>:WITH_WHISPERCPP=0>"
			"$<$<NOT:$<CONFIG:Debug>>:WITH_WHISPERCPP=1>")
	else()
		set(GK_WHISPER_LINK_LIBS whisper)
		list(APPEND GK_WHISPER_COMPILE_DEFINITIONS WITH_WHISPERCPP=1)
	endif()
endif()

# postprocess: target setup
if ( ANDROID )
set(AllTargets
gkNextRenderer
)
elseif( IOS )
set(AllTargets
gkNextEngine
NextGameplay
gkNextRenderer
${GK_MODULE_TARGETS}
)
else()
set(AllTargets
gkNextEngine
NextGameplay
gkNextRenderer
gkNextStillBenchmark
	gkNextMotionBenchmark
	gkNextVisualTest
	RmlUiDemo
	gkNextEditor
	ScadStudio
	Packager
	MagicaLego
	KongLie3D
	Brotato3D
	FlappyCpp
	FlappyJs
	BrickPlayer
	CharacterDemo
	StudioSim
	gkNextUnitTests
	${GK_MODULE_TARGETS}
)
endif()

# Unity Build option (default ON, can be disabled with -DENABLE_UNITY_BUILD=OFF)
option(ENABLE_UNITY_BUILD "Enable unity builds for engine target" ON)
file(TO_CMAKE_PATH "${Vulkan_SLANGC}" GK_SLANGC_EXECUTABLE_PATH)

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
	if (GK_ENABLE_HOT_RELOAD)
		target_compile_definitions(${target} PUBLIC GK_ENABLE_HOT_RELOAD=1)
	else()
		target_compile_definitions(${target} PUBLIC GK_ENABLE_HOT_RELOAD=0)
	endif()
	if (GK_ENABLE_SHADER_CLOCK)
		target_compile_definitions(${target} PUBLIC GK_ENABLE_SHADER_CLOCK=1)
	else()
		target_compile_definitions(${target} PUBLIC GK_ENABLE_SHADER_CLOCK=0)
	endif()
	target_compile_definitions(${target} PUBLIC GK_WITH_RMLUI=1)
	if (GK_REMOTE_ENABLED)
        target_compile_definitions(${target} PUBLIC GK_WITH_REMOTE=1)
	else()
        target_compile_definitions(${target} PUBLIC GK_WITH_REMOTE=0)
	endif()

	if( IOS )
	target_compile_definitions(${target} PUBLIC IOS=1)
	endif()

	# link engine
	if ( ${target} STREQUAL gkNextEngine )
		target_compile_definitions(${target} PRIVATE ENGINE_EXPORTS)
		target_compile_definitions(${target} PRIVATE GK_NEXT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
		target_compile_definitions(${target} PRIVATE GK_SLANGC_EXECUTABLE="${GK_SLANGC_EXECUTABLE_PATH}")
		
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
		set_source_files_properties(Engine/Assets/Loaders/FSceneLoader.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		set_source_files_properties(Engine/Assets/Savers/FSceneSaver.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		set_source_files_properties(Engine/Vulkan/Allocator.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		# Isolated 3rd-party-heavy SCAD sources (FreeType / earcut) kept out of unity.
		set_source_files_properties(Engine/Assets/Loaders/FScadText.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		set_source_files_properties(Engine/Assets/Loaders/FScadTess.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		endif()
		find_package(WebP CONFIG REQUIRED)
		target_link_libraries(${target} PRIVATE SDL3::SDL3 xxHash::xxhash meshoptimizer::meshoptimizer fmt::fmt CURL::libcurl glm::glm imgui::imgui draco::draco WebP::webp GPUOpen::VulkanMemoryAllocator ${extra_libs})
		target_link_libraries(${target} PUBLIC spdlog::spdlog)
		find_package(RmlUi CONFIG REQUIRED)
		target_link_libraries(${target} PRIVATE RmlUi::RmlUi)
		if (GK_REMOTE_ENABLED)
            target_link_libraries(${target} PRIVATE ${GK_REMOTE_LINK_LIBS})
		endif()

		# Manifold: robust CSG boolean backend for the SCAD loader (desktop only).
		find_package(manifold CONFIG QUIET)
		if ( manifold_FOUND AND NOT GK_DISABLE_MANIFOLD )
			# Manifold's imported target advertises cxx_std_17 + -D flags through its
			# INTERFACE, which clobbers the engine's C++20 standard. Strip those and
			# re-apply only the defines the headers need as proper definitions.
			set_target_properties(manifold::manifold PROPERTIES
				INTERFACE_COMPILE_FEATURES ""
				INTERFACE_COMPILE_OPTIONS "")
			target_link_libraries(${target} PRIVATE manifold::manifold)
			target_compile_definitions(${target} PRIVATE GK_WITH_MANIFOLD=1 MANIFOLD_CROSS_SECTION MANIFOLD_PAR=-1)
			message(STATUS "Manifold found: SCAD CSG boolean backend enabled")
		else()
			target_compile_definitions(${target} PRIVATE GK_WITH_MANIFOLD=0)
			message(STATUS "Manifold not found: SCAD CSG falls back to first-child approximation")
		endif()

		# FreeType: SCAD text() glyph outlines (earcut header comes from the same
		# vcpkg include root). FScadText.cpp also includes <mapbox/earcut.hpp>.
		find_package(Freetype QUIET)
		if ( Freetype_FOUND AND NOT GK_DISABLE_SCAD_TEXT )
			target_link_libraries(${target} PRIVATE Freetype::Freetype)
			target_compile_definitions(${target} PRIVATE GK_WITH_FREETYPE=1)
			message(STATUS "FreeType found: SCAD text() enabled")
		else()
			target_compile_definitions(${target} PRIVATE GK_WITH_FREETYPE=0)
			message(STATUS "FreeType not found: SCAD text() skipped")
		endif()

		# earcut-hpp (header-only, from the vcpkg include root) for concave/holed
		# polygon tessellation. Desktop only (matches the vcpkg.json platform gate).
		if ( NOT IOS )
			target_compile_definitions(${target} PRIVATE GK_WITH_EARCUT=1)
		else()
			target_compile_definitions(${target} PRIVATE GK_WITH_EARCUT=0)
		endif()
		if ( GK_WHISPER_LINK_LIBS )
			target_link_libraries(${target} PRIVATE ${GK_WHISPER_LINK_LIBS})
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
			if ( WIN32 )
				target_link_libraries(${target} PRIVATE delayimp)
				target_link_options(${target} PRIVATE "/DELAYLOAD:sl.interposer.dll")
			endif()
		endif()
	elseif ( ${target} STREQUAL NextGameplay )
		set_target_properties(${target} PROPERTIES FOLDER "Gameplay")
		target_link_libraries(${target} PRIVATE gkNextEngine)
	elseif ( ${target} IN_LIST GK_MODULE_TARGETS )
		set_target_properties(${target} PROPERTIES FOLDER "Modules")
		target_link_libraries(${target} PRIVATE gkNextEngine)
	else()
		set_target_properties(${target} PROPERTIES FOLDER "Programs")
		if ( NOT ANDROID ) 
		target_link_libraries(${target} PRIVATE gkNextEngine)
		endif()
		if ( WITH_STREAMLINE ) 
		target_link_directories(${target} PRIVATE ${STREAMLINE_LIB_DIR})
		target_link_libraries(${target} PRIVATE sl.interposer)
		if ( WIN32 )
			target_link_libraries(${target} PRIVATE delayimp)
			target_link_options(${target} PRIVATE "/DELAYLOAD:sl.interposer.dll")
		endif()
		endif()
	endif()

	# for android
	if ( ANDROID )
		find_package(WebP CONFIG REQUIRED)
		find_package(RmlUi CONFIG REQUIRED)
		target_link_options(${target} PRIVATE
	    -Wl,-z,max-page-size=16384
		-Wl,-z,common-page-size=16384
		-Wl,--hash-style=gnu
		)
		target_link_libraries(${target} PRIVATE android SDL3::SDL3 spdlog::spdlog xxHash::xxhash meshoptimizer::meshoptimizer fmt::fmt CURL::libcurl glm::glm imgui::imgui draco::draco WebP::webp RmlUi::RmlUi ${Vulkan_LIBRARIES} ${extra_libs})
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
	else()
		target_compile_definitions(${target} PUBLIC WITH_SUPERLUMINAL=0)
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

	if ( GK_WHISPER_COMPILE_DEFINITIONS )
		target_compile_definitions(${target} PUBLIC ${GK_WHISPER_COMPILE_DEFINITIONS})
	endif()
	if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
		target_compile_definitions(${target} PRIVATE JSON_HAS_FILESYSTEM=0 JSON_HAS_EXPERIMENTAL_FILESYSTEM=0)
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
		if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(${target} PRIVATE -Wno-unknown-warning-option)
		endif()
		target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-but-set-variable -Wno-inconsistent-missing-override -Wno-unused-const-variable -Wno-unused-variable -Wno-unused-private-field -Wno-unused-function -Wno-sign-compare -Wno-unused-result)
		if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			target_compile_options(${target} PRIVATE -Wno-stringop-overflow -Wno-ignored-attributes)
		endif()
	endif()

	# specific for targets
	if ( UNIX AND NOT APPLE AND NOT ANDROID)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -mavx")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mavx")
	endif()

	if ( ${target} STREQUAL MagicaLego )
	if (MSVC)
		set(FFMPEG_EXE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../src/ThirdParty/ffmpeg/bin/ffmpeg.exe)
		if (EXISTS ${FFMPEG_EXE_PATH})
			file(COPY ${FFMPEG_EXE_PATH} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
		else()
			message(STATUS "ffmpeg.exe not found at ${FFMPEG_EXE_PATH}; run `gnb paks fetch ffmpeg` if you need MagicaLego video capture.")
		endif()
	endif()
	endif()

	if ( ${target} STREQUAL gkNextEngine )
	if ( WITH_STREAMLINE )
		file(GLOB SL_BINS "${STREAMLINE_BIN_DIR}/*")
		file(COPY ${SL_BINS} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
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

if ( TARGET CharacterDemo )
target_link_libraries(CharacterDemo PRIVATE NextGameplay)
endif()
if ( TARGET StudioSim )
target_link_libraries(StudioSim PRIVATE NextGameplay)
endif()

# Test Target
if ( NOT ANDROID AND NOT IOS )
target_link_libraries(gkNextUnitTests PRIVATE NextGameplay gkNextEngine Catch2::Catch2WithMain gk_project_options)
set_target_properties(gkNextUnitTests PROPERTIES FOLDER "Tests")
endif()



set_target_properties(ImNodeFlow PROPERTIES FOLDER "ThirdParty")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(ImNodeFlow PRIVATE -include cstdint)
endif()

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
