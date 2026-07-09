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
	Tests/Test_HdrTextureCache.cpp
	Tests/Test_ComponentSystem.cpp
	Tests/Test_GameplayComponents.cpp
	Tests/Test_LDrawConfig.cpp
	Tests/Test_LDrawGeometry.cpp
	Tests/Test_LDrawLoader.cpp
	Tests/Test_LDrawParser.cpp
	Tests/Test_LDrawConnectivity.cpp
	Tests/Test_CVarSystem.cpp
	Tests/Test_SceneList.cpp
	Tests/Test_SceneReference.cpp
	Tests/Test_ScadLoader.cpp
	Tests/Test_SimKit.cpp
	Tests/Test_ScadRig.cpp
	Tests/Test_RigAnimator.cpp
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
	Tests/Test_NextRAFixed.cpp
	Application/Game/NextRA/Net/LoopbackTransport.cpp
	Application/Game/NextRA/Net/Order.cpp
	Application/Game/NextRA/Net/OrderManager.cpp
	Application/Game/NextRA/Net/Replay.cpp
	Application/Game/NextRA/Sim/OccupancyGrid.cpp
	Application/Game/NextRA/Sim/PathfindGrid.cpp
	Application/Game/NextRA/Sim/SimWorld.cpp
	Application/Game/NextRA/Sim/SyncHash.cpp
	Application/Game/NextRA/Sim/Systems/OrderApplySystem.cpp
	Application/Game/Brotato3D/Brotato3DPcgGenerator.cpp
	Tests/Test_AIChatProtocol.cpp
	Tests/Test_LlamaPidFile.cpp
	Tests/Test_AgentLoop.cpp
	Tests/Test_RepoTools.cpp
	Tests/Test_ScadPromptContext.cpp
	Tests/Test_ScadSessionStore.cpp
	Tests/Test_Rgb9e5Pack.cpp
	Tests/Test_UpscalerTypes.cpp
	Tests/Test_SogLoader.cpp
	Tests/Test_TerminalBlitter.cpp
	Application/Editor/ScadStudio/ScadPromptContext.cpp
	Application/Editor/ScadStudio/ScadSessionStore.cpp
)

list(APPEND UNIT_TEST_SOURCES Tests/Test_PhysicsComponent.cpp)

# Desktop entry point shared by desktop applications.
set(DESKTOP_MAIN_SOURCES
	DesktopMain.cpp
)

option(ENABLE_UNITY_BUILD "Enable unity builds for engine/modules/app targets" ON)
option(GK_FAST_DEV_LINK "Prefer faster MSVC executable links for development builds" ON)

function(gk_enable_unity_build target batch_size)
	if ( ENABLE_UNITY_BUILD AND NOT ANDROID )
		set_target_properties(${target} PROPERTIES
			UNITY_BUILD ON
			UNITY_BUILD_BATCH_SIZE ${batch_size}
		)
	endif()
endfunction()

function(gk_enable_fast_dev_link target)
	if ( MSVC AND GK_FAST_DEV_LINK )
		target_link_options(${target} PRIVATE
			/INCREMENTAL
			/OPT:NOREF
			/OPT:NOICF
		)
	endif()
endfunction()


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
	${src_files_appcommon}
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
	${src_files_appcommon}
	${DESKTOP_MAIN_SOURCES}
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
	${src_files_appcommon}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(gkNextRenderer PRIVATE Application/Render/gkNextRenderer)
add_executable(gkNextStillBenchmark
	${src_files_benchmarkcommon}
	${src_files_gkstillbenchmark}
	${src_files_appcommon}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(gkNextStillBenchmark PRIVATE Application/Render/gkNextBenchmark)
add_executable(gkNextMotionBenchmark
	${src_files_benchmarkcommon}
	${src_files_gkmotionbenchmark}
	${src_files_appcommon}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(gkNextMotionBenchmark PRIVATE Application/Render/gkNextBenchmark)
add_executable(gkNextVisualTest
	${src_files_gkvisualtest}
	${src_files_appcommon}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(gkNextVisualTest PRIVATE Application/Render/gkNextVisualTest)
add_executable(RmlUiDemo
	${src_files_rmluidemo}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(RmlUiDemo PRIVATE Application/Render/RmlUiDemo)
add_executable(gkNextEditor
	${src_files_editorcommon}
	${src_files_editor}
	${src_files_appcommon}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(gkNextEditor PRIVATE Application/Editor/gkNextEditor)
add_executable(ScadStudio
	${src_files_scadstudio}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(ScadStudio PRIVATE Application/Editor/ScadStudio)
add_executable(MagicaLego
	${src_files_magicalego}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(MagicaLego PRIVATE Application/Game/MagicaLego)
add_executable(KongLie3D
	${src_files_konglie3d}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(KongLie3D PRIVATE Application/Game/KongLie3D)
add_executable(Brotato3D
	${src_files_brotato3d}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(Brotato3D PRIVATE Application/Game/Brotato3D)
target_compile_definitions(Brotato3D PRIVATE DEV_MODE=1)
add_executable(NextRA
	${src_files_nextra}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(NextRA PRIVATE Application/Game/NextRA)
target_compile_definitions(NextRA PRIVATE DEV_MODE=1)
add_executable(FlappyCpp
	${src_files_flappycpp}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(FlappyCpp PRIVATE Application/Game/Flappy)
add_executable(FlappyJs
	${src_files_flappyjs}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(FlappyJs PRIVATE Application/Game/Flappy)
add_executable(BrickPlayer
	${src_files_brickplayer}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(BrickPlayer PRIVATE Application/Game/BrickPlayer)
add_executable(CharacterDemo
	${src_files_characterdemo}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(CharacterDemo PRIVATE Application/Game/CharacterDemo)
add_executable(StudioSim
	${src_files_studiosim}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(StudioSim PRIVATE Application/Game/StudioSim)
add_executable(AirportSim
	${src_files_airportsim}
	${DESKTOP_MAIN_SOURCES}
)
target_include_directories(AirportSim PRIVATE Application/Game/AirportSim)
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
		gk_enable_unity_build(${gk_module} 8)
		list(APPEND GK_MODULE_TARGETS ${gk_module})
	endif()
endforeach()
endif()

if ( TARGET ScadLoader )
	# Manifold: robust CSG boolean backend for the SCAD loader (desktop only).
	find_package(manifold CONFIG QUIET)
	if ( manifold_FOUND AND NOT GK_DISABLE_MANIFOLD )
		# Manifold's imported target advertises cxx_std_17 + -D flags through its
		# INTERFACE, which clobbers the engine's C++20 standard. Strip those and
		# re-apply only the defines the headers need as proper definitions.
		set_target_properties(manifold::manifold PROPERTIES
			INTERFACE_COMPILE_FEATURES ""
			INTERFACE_COMPILE_OPTIONS "")
		target_link_libraries(ScadLoader PRIVATE manifold::manifold)
		target_compile_definitions(ScadLoader PRIVATE GK_WITH_MANIFOLD=1 MANIFOLD_CROSS_SECTION MANIFOLD_PAR=-1)
		message(STATUS "Manifold found: SCAD CSG boolean backend enabled")
	else()
		target_compile_definitions(ScadLoader PRIVATE GK_WITH_MANIFOLD=0)
		message(STATUS "Manifold not found: SCAD CSG falls back to first-child approximation")
	endif()

	# FreeType: SCAD text() glyph outlines (earcut header comes from the same
	# vcpkg include root). FScadText.cpp also includes <mapbox/earcut.hpp>.
	find_package(Freetype QUIET)
	if ( Freetype_FOUND AND NOT GK_DISABLE_SCAD_TEXT )
		if ( TARGET freetype )
			target_link_libraries(ScadLoader PRIVATE freetype)
		else()
			target_link_libraries(ScadLoader PRIVATE Freetype::Freetype)
		endif()
		target_compile_definitions(ScadLoader PRIVATE GK_WITH_FREETYPE=1)
		message(STATUS "FreeType found: SCAD text() enabled")
	else()
		target_compile_definitions(ScadLoader PRIVATE GK_WITH_FREETYPE=0)
		message(STATUS "FreeType not found: SCAD text() skipped")
	endif()

	# earcut-hpp (header-only, from the vcpkg include root) for concave/holed
	# polygon tessellation. Desktop only (matches the vcpkg.json platform gate).
	if ( NOT IOS )
		target_compile_definitions(ScadLoader PRIVATE GK_WITH_EARCUT=1)
	else()
		target_compile_definitions(ScadLoader PRIVATE GK_WITH_EARCUT=0)
	endif()
endif()

if ( TARGET SplatLoader )
	find_package(WebP CONFIG REQUIRED)
	find_package(ZLIB REQUIRED)
	target_link_libraries(SplatLoader PRIVATE WebP::webp ZLIB::ZLIB)
endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

add_subdirectory(ThirdParty/ozz)

if(NOT ANDROID)
add_subdirectory(ThirdParty/quickjs-ng)
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

    if (GK_REMOTE_LIBDATACHANNEL_TARGET AND TARGET httplib::httplib)
        set(GK_REMOTE_ENABLED ON)
        list(APPEND GK_REMOTE_LINK_LIBS ${GK_REMOTE_LIBDATACHANNEL_TARGET} httplib::httplib)
        message(STATUS "Remote Play enabled: ${GK_REMOTE_LIBDATACHANNEL_TARGET} + httplib::httplib")
    else()
        message(STATUS "Remote Play disabled: libdatachannel/cpp-httplib targets not found")
    endif()
else()
    message(STATUS "Remote Play disabled for this platform or by GK_WITH_REMOTE=OFF")
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
	ScadStudio
	Packager
	MagicaLego
	KongLie3D
	Brotato3D
	NextRA
	FlappyCpp
	BrickPlayer
	CharacterDemo
	StudioSim
	AirportSim
	gkNextUnitTests
	${GK_MODULE_TARGETS}
)
foreach(gk_quickjs_program gkNextEditor FlappyJs)
	if ( TARGET ${gk_quickjs_program} )
		list(APPEND AllTargets ${gk_quickjs_program})
	endif()
endforeach()
endif()

file(TO_CMAKE_PATH "${Vulkan_SLANGC}" GK_SLANGC_EXECUTABLE_PATH)

function(gk_link_engine_feature_dependencies target)
	if( WITH_SUPERLUMINAL )
		target_compile_definitions(${target} PUBLIC WITH_SUPERLUMINAL=1)
		target_include_directories(${target} SYSTEM PUBLIC ${SuperluminalAPI_INCLUDE_DIRS})
		target_link_libraries(${target} PRIVATE SuperluminalAPI)
		if ( MSVC )
			target_link_options(${target} PRIVATE /ignore:4099)
		endif()
	else()
		target_compile_definitions(${target} PUBLIC WITH_SUPERLUMINAL=0)
	endif()

	target_include_directories(${target} PUBLIC $<TARGET_PROPERTY:Jolt::Jolt,INTERFACE_INCLUDE_DIRECTORIES>)
	target_link_libraries(${target} PRIVATE Jolt::Jolt )

	target_link_libraries(${target} PRIVATE ozz )

	target_link_libraries(${target} PRIVATE KTX::ktx )

	if ( WITH_AVIF )
		target_compile_definitions(${target} PUBLIC WITH_AVIF=1 AVIF_CODEC_AOM=SYSTEM)
		target_include_directories(${target} PUBLIC $<TARGET_PROPERTY:avif,INTERFACE_INCLUDE_DIRECTORIES>)
		target_link_libraries(${target} PRIVATE avif)
	endif()
endfunction()

# common setup
foreach(target IN LISTS AllTargets)
	# common dependencies
	add_dependencies(${target} Assets)
	set(gk_target_is_engine_runtime OFF)
	if ( "${target}" STREQUAL "gkNextEngine" OR (ANDROID AND "${target}" STREQUAL "gkNextRenderer") )
		set(gk_target_is_engine_runtime ON)
	endif()

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
	if (GK_WITH_TUI)
		target_compile_definitions(${target} PUBLIC GK_WITH_TUI=1)
	else()
		target_compile_definitions(${target} PUBLIC GK_WITH_TUI=0)
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
		set_source_files_properties(Engine/Utilities/StbImage.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		set_source_files_properties(Engine/Assets/Acceleration/CpuBvh.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
		endif()
		find_package(WebP CONFIG REQUIRED)
		target_link_libraries(${target} PRIVATE SDL3::SDL3 xxHash::xxhash meshoptimizer::meshoptimizer fmt::fmt CURL::libcurl glm::glm imgui::imgui draco::draco WebP::webp GPUOpen::VulkanMemoryAllocator ${extra_libs})
		target_link_libraries(${target} PUBLIC spdlog::spdlog)

		
		# handle moltenvk for iOS
		if ( IOS )
		get_target_property(_ifaces imgui::imgui INTERFACE_LINK_LIBRARIES)
		list(REMOVE_ITEM _ifaces Vulkan::Vulkan)
		set_target_properties(imgui::imgui PROPERTIES INTERFACE_LINK_LIBRARIES "${_ifaces}")
		target_link_libraries(${target} PRIVATE ${MOLTENVK_LIBRARY})
		elseif ( WIN32 AND WITH_STREAMLINE )
		get_target_property(_ifaces imgui::imgui INTERFACE_LINK_LIBRARIES)
		list(REMOVE_ITEM _ifaces Vulkan::Vulkan)
		set_target_properties(imgui::imgui PROPERTIES INTERFACE_LINK_LIBRARIES "${_ifaces}")
		target_link_directories(${target} PRIVATE ${STREAMLINE_LIB_DIR})
		target_link_libraries(${target} PRIVATE "${STREAMLINE_LIB_DIR}/sl.interposer.lib")
		else()
		target_link_libraries(${target} PRIVATE ${Vulkan_LIBRARIES})
		endif()

		if ( NOT ANDROID AND NOT IOS )
		target_link_libraries(${target} PRIVATE cpptrace::cpptrace)
		endif()

		if ( WITH_STREAMLINE )
			target_compile_definitions(${target} PUBLIC WITH_STREAMLINE=1)
			target_compile_definitions(${target} PRIVATE GK_STREAMLINE_INTERPOSER_DLL="${STREAMLINE_BIN_DIR}/sl.interposer.dll")
			target_include_directories(${target} PRIVATE ${STREAMLINE_INCLUDE_DIR})
			target_link_directories(${target} PRIVATE ${STREAMLINE_LIB_DIR})
		endif()

		gk_link_engine_feature_dependencies(${target})
	elseif ( ${target} STREQUAL NextGameplay )
		set_target_properties(${target} PROPERTIES FOLDER "Gameplay")
		gk_enable_unity_build(${target} 8)
		target_link_libraries(${target} PRIVATE gkNextEngine)
	elseif ( ${target} IN_LIST GK_MODULE_TARGETS )
		set_target_properties(${target} PROPERTIES FOLDER "Modules")
		target_link_libraries(${target} PRIVATE gkNextEngine)
	else()
		set_target_properties(${target} PROPERTIES FOLDER "Programs")
		if ( NOT ANDROID )
		target_link_libraries(${target} PRIVATE gkNextEngine)
		get_target_property(_target_type ${target} TYPE)
		if ( _target_type STREQUAL "EXECUTABLE" AND NOT ${target} STREQUAL Packager AND NOT ${target} STREQUAL gkNextUnitTests )
			gk_enable_unity_build(${target} 8)
			gk_enable_fast_dev_link(${target})
		endif()
		# DesktopMain registers the DevTools debug UI provider for every program.
		if ( TARGET DevTools )
		target_link_libraries(${target} PRIVATE DevTools)
		endif()
		if ( TARGET LiveCoding AND _target_type STREQUAL "EXECUTABLE" AND NOT ${target} STREQUAL Packager AND NOT ${target} STREQUAL gkNextUnitTests )
		target_link_libraries(${target} PRIVATE LiveCoding)
		endif()
		# DesktopMain installs the agent script driver (gnb shot / gnb validate) for every program.
		if ( TARGET AgentDriver AND _target_type STREQUAL "EXECUTABLE" AND NOT ${target} STREQUAL Packager AND NOT ${target} STREQUAL gkNextUnitTests )
		target_link_libraries(${target} PRIVATE AgentDriver)
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
		if ( gk_target_is_engine_runtime )
			gk_link_engine_feature_dependencies(${target})
		endif()
	endif()

	if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
		target_compile_definitions(${target} PRIVATE JSON_HAS_FILESYSTEM=0 JSON_HAS_EXPERIMENTAL_FILESYSTEM=0)
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
	# third-party warning fix
	set_source_files_properties(
		Application/Editor/gkNextEditor/AI/EditorAIService.cpp
		Application/Editor/gkNextEditor/AI/EditorScriptExecutor.cpp
		PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
	)
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
if ( TARGET AirportSim )
target_link_libraries(AirportSim PRIVATE NextGameplay)
endif()

if ( TARGET NextQuickJS )
	target_compile_definitions(NextQuickJS PRIVATE GK_NEXT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
	target_link_libraries(NextQuickJS PRIVATE quickjs SDL3::SDL3 imgui::imgui)
	foreach(t FlappyJs gkNextEditor)
		if ( TARGET ${t} )
			target_link_libraries(${t} PRIVATE NextQuickJS)
		endif()
	endforeach()
endif()

# Optional loader modules: linked only by the applications that use them.
if ( TARGET LDrawLoader )
	foreach(t gkNextRenderer gkNextVisualTest gkNextStillBenchmark gkNextMotionBenchmark gkNextEditor BrickPlayer gkNextUnitTests)
		if ( TARGET ${t} )
			target_link_libraries(${t} PRIVATE LDrawLoader)
		endif()
	endforeach()
endif()
if ( TARGET ScadLoader )
	foreach(t NextGameplay gkNextRenderer gkNextVisualTest gkNextStillBenchmark gkNextMotionBenchmark gkNextEditor ScadStudio StudioSim AirportSim gkNextUnitTests)
		if ( TARGET ${t} )
			target_link_libraries(${t} PRIVATE ScadLoader)
		endif()
	endforeach()
endif()

if ( TARGET SplatLoader )
	foreach(t gkNextRenderer gkNextVisualTest gkNextEditor gkNextUnitTests)
		if ( TARGET ${t} )
			target_link_libraries(${t} PRIVATE SplatLoader)
		endif()
	endforeach()
endif()
if ( TARGET NextAI )
	foreach(t NextGameplay gkNextEditor ScadStudio MagicaLego StudioSim AirportSim gkNextUnitTests)
		if ( TARGET ${t} )
			target_link_libraries(${t} PRIVATE NextAI)
		endif()
	endforeach()
endif()
if ( TARGET NextRmlUi )
	find_package(RmlUi CONFIG REQUIRED)
	target_link_libraries(NextRmlUi PRIVATE RmlUi::RmlUi)
	# DesktopMain installs the RmlUi overlay factory for every program.
	foreach(t IN LISTS AllTargets)
		if ( NOT ${t} STREQUAL gkNextEngine AND TARGET ${t} )
			get_target_property(_ttype ${t} TYPE)
			if ( _ttype STREQUAL "EXECUTABLE" )
				target_link_libraries(${t} PRIVATE NextRmlUi)
			endif()
		endif()
	endforeach()
endif()
if ( TARGET RenderViews )
	# Shared offscreen render-view provider (editor camera views + NextRemote streaming).
	if ( TARGET NextRemote )
		target_link_libraries(NextRemote PRIVATE RenderViews)
	endif()
	target_link_libraries(gkNextEditor PRIVATE RenderViews)
endif()
if ( TARGET NextRemote )
	if (GK_REMOTE_ENABLED)
		target_link_libraries(NextRemote PRIVATE ${GK_REMOTE_LINK_LIBS})
	endif()
	# DesktopMain assembles the remote frame streamer for every program.
	foreach(t IN LISTS AllTargets)
		if ( NOT ${t} STREQUAL gkNextEngine AND NOT ${t} STREQUAL NextRemote AND TARGET ${t} )
			get_target_property(_ttype ${t} TYPE)
			if ( _ttype STREQUAL "EXECUTABLE" )
				target_link_libraries(${t} PRIVATE NextRemote)
			endif()
		endif()
	endforeach()
endif()
if ( TARGET NextTui )
	foreach(t IN LISTS AllTargets)
		if ( NOT ${t} STREQUAL gkNextEngine AND NOT ${t} STREQUAL NextTui AND TARGET ${t} )
			get_target_property(_ttype ${t} TYPE)
			if ( _ttype STREQUAL "EXECUTABLE" )
				target_link_libraries(${t} PRIVATE NextTui)
			endif()
		endif()
	endforeach()
endif()

# Test Target
if ( NOT ANDROID AND NOT IOS )
target_link_libraries(gkNextUnitTests PRIVATE NextGameplay gkNextEngine Catch2::Catch2WithMain)
target_include_directories(gkNextUnitTests PRIVATE Application/Game/NextRA)
set_target_properties(gkNextUnitTests PROPERTIES FOLDER "Tests")
endif()

if ( TARGET quickjs )
set_target_properties(quickjs PROPERTIES FOLDER "ThirdParty")
endif()

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
