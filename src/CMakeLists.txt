set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(src_files_assets
	Assets/CornellBox.cpp
	Assets/CornellBox.hpp
	Assets/Material.hpp
	Assets/Model.cpp
	Assets/Model.hpp
	Assets/Procedural.hpp
	Assets/Scene.cpp
	Assets/Scene.hpp
	Assets/Sphere.hpp
	Assets/Texture.cpp
	Assets/Texture.hpp
	Assets/TextureImage.cpp
	Assets/TextureImage.hpp
	Assets/UniformBuffer.cpp
	Assets/UniformBuffer.hpp
	Assets/Vertex.hpp
)

set(src_files_utilities
	Utilities/Console.cpp
	Utilities/Console.hpp
	Utilities/Exception.hpp
	Utilities/FileHelper.hpp
	Utilities/Math.hpp
	Utilities/Glm.hpp
	Utilities/StbImage.cpp
	Utilities/StbImage.hpp
	Utilities/Localization.hpp
	Utilities/Localization.cpp
	Utilities/ImGui.hpp
)

set(src_files_vulkan
	Vulkan/VulkanBaseRenderer.cpp
	Vulkan/VulkanBaseRenderer.hpp
	Vulkan/Buffer.cpp
	Vulkan/Buffer.hpp
	Vulkan/BufferUtil.hpp
	Vulkan/CommandBuffers.cpp
	Vulkan/CommandBuffers.hpp
	Vulkan/CommandPool.cpp
	Vulkan/CommandPool.hpp
	Vulkan/DebugUtils.cpp
	Vulkan/DebugUtils.hpp
	Vulkan/DebugUtilsMessenger.cpp
	Vulkan/DebugUtilsMessenger.hpp
	Vulkan/DepthBuffer.cpp
	Vulkan/DepthBuffer.hpp
	Vulkan/DescriptorBinding.hpp
	Vulkan/DescriptorPool.cpp
	Vulkan/DescriptorPool.hpp
	Vulkan/DescriptorSetLayout.cpp
	Vulkan/DescriptorSetLayout.hpp
	Vulkan/DescriptorSetManager.cpp
	Vulkan/DescriptorSetManager.hpp
	Vulkan/DescriptorSets.cpp
	Vulkan/DescriptorSets.hpp
	Vulkan/Device.cpp
	Vulkan/Device.hpp
	Vulkan/DeviceMemory.cpp
	Vulkan/DeviceMemory.hpp
	Vulkan/Enumerate.hpp
	Vulkan/Fence.cpp
	Vulkan/Fence.hpp
	Vulkan/FrameBuffer.cpp
	Vulkan/FrameBuffer.hpp
	Vulkan/GraphicsPipeline.cpp
	Vulkan/GraphicsPipeline.hpp
	Vulkan/Image.cpp
	Vulkan/Image.hpp	
	Vulkan/ImageMemoryBarrier.hpp	
	Vulkan/ImageView.cpp
	Vulkan/ImageView.hpp	
	Vulkan/Instance.cpp
	Vulkan/Instance.hpp
	Vulkan/PipelineLayout.cpp
	Vulkan/PipelineLayout.hpp
	Vulkan/RenderPass.cpp
	Vulkan/RenderPass.hpp
	Vulkan/RenderImage.cpp
	Vulkan/RenderImage.hpp
	Vulkan/Sampler.cpp
	Vulkan/Sampler.hpp
	Vulkan/Semaphore.cpp
	Vulkan/Semaphore.hpp
	Vulkan/ShaderModule.cpp
	Vulkan/ShaderModule.hpp	
	Vulkan/SingleTimeCommands.hpp
	Vulkan/Strings.cpp
	Vulkan/Strings.hpp	
	Vulkan/Surface.cpp
	Vulkan/Surface.hpp	
	Vulkan/SwapChain.cpp
	Vulkan/SwapChain.hpp
	Vulkan/Version.hpp
	Vulkan/Vulkan.cpp
	Vulkan/Vulkan.hpp
	Vulkan/Window.cpp
	Vulkan/Window.hpp
	Vulkan/WindowConfig.hpp
)

set(src_files_vulkan_raytracing
	Vulkan/RayTracing/RayTraceBaseRenderer.cpp
	Vulkan/RayTracing/RayTraceBaseRenderer.hpp
	Vulkan/RayTracing/AccelerationStructure.cpp
	Vulkan/RayTracing/AccelerationStructure.hpp
	Vulkan/RayTracing/BottomLevelAccelerationStructure.cpp
	Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp
	Vulkan/RayTracing/BottomLevelGeometry.cpp
	Vulkan/RayTracing/BottomLevelGeometry.hpp
	Vulkan/RayTracing/DeviceProcedures.cpp
	Vulkan/RayTracing/DeviceProcedures.hpp
	Vulkan/RayTracing/RayTracingProperties.cpp
	Vulkan/RayTracing/RayTracingProperties.hpp
	Vulkan/RayTracing/ShaderBindingTable.cpp
	Vulkan/RayTracing/ShaderBindingTable.hpp
	Vulkan/RayTracing/TopLevelAccelerationStructure.cpp
	Vulkan/RayTracing/TopLevelAccelerationStructure.hpp
)

set(src_files_vulkan_rayquery
	Vulkan/RayQuery/RayQueryRenderer.cpp
	Vulkan/RayQuery/RayQueryRenderer.hpp
	Vulkan/RayQuery/RayQueryPipeline.cpp
	Vulkan/RayQuery/RayQueryPipeline.hpp
)

set(src_files_vulkan_legacydeferred
	Vulkan/LegacyDeferred/LegacyDeferredRenderer.cpp
	Vulkan/LegacyDeferred/LegacyDeferredRenderer.hpp
	Vulkan/LegacyDeferred/LegacyDeferredPipeline.cpp
	Vulkan/LegacyDeferred/LegacyDeferredPipeline.hpp
)

set(src_files_vulkan_moderndeferred
	Vulkan/ModernDeferred/ModernDeferredRenderer.cpp
	Vulkan/ModernDeferred/ModernDeferredRenderer.hpp
	Vulkan/ModernDeferred/ModernDeferredPipeline.cpp
	Vulkan/ModernDeferred/ModernDeferredPipeline.hpp
)

set(src_files_vulkan_hybriddeferred
	Vulkan/HybridDeferred/HybridDeferredRenderer.cpp
	Vulkan/HybridDeferred/HybridDeferredRenderer.hpp
	Vulkan/HybridDeferred/HybridDeferredPipeline.cpp
	Vulkan/HybridDeferred/HybridDeferredPipeline.hpp
)


set(src_files_common_compute_pipeline
	Vulkan/PipelineCommon/CommonComputePipeline.cpp
	Vulkan/PipelineCommon/CommonComputePipeline.hpp
)

set(src_files_thirdparty_json11
	ThirdParty/json11/json11.cpp
	ThirdParty/json11/json11.hpp
)

if (MSVC)
set_source_files_properties(
  ${src_files_thirdparty_json11}
  PROPERTIES
  COMPILE_FLAGS "/W0"
) 
else()
set_source_files_properties(
  ${src_files_thirdparty_json11}
  PROPERTIES
  COMPILE_FLAGS "-w"
) 
endif()

set(src_files_editor
		Editor/EditorInterface.cpp
		Editor/EditorInterface.hpp
		Editor/EditorMain.cpp
		Editor/EditorMain.h
        Editor/EditorGUI.h
		Editor/EditorMenubar.cpp
		Editor/EditorProperties.cpp
		Editor/EditorOutliner.cpp
		Editor/EditorViewport.cpp
		Editor/EditorContentBrowser.cpp
		Editor/EditorUtils.cpp
		Editor/EditorUtils.h
		Editor/EditorCommand.hpp
		Editor/EditorMaterialEd.cpp
		Editor/IconsFontAwesome6.h
		Editor/Nodes/NodeSetFloat.cpp
		Editor/Nodes/NodeSetFloat.hpp
		Editor/Nodes/NodeSetInt.cpp
		Editor/Nodes/NodeSetInt.hpp
		Editor/Nodes/NodeMaterial.cpp
		Editor/Nodes/NodeMaterial.hpp
)

set(src_files_runtime
	Runtime/ModelViewController.cpp
	Runtime/ModelViewController.hpp
	Runtime/Application.cpp
	Runtime/Application.hpp
	Runtime/SceneList.cpp
	Runtime/SceneList.hpp
	Runtime/UserInterface.cpp
	Runtime/UserInterface.hpp
	Runtime/UserSettings.hpp
	Runtime/TaskCoordinator.cpp
	Runtime/TaskCoordinator.hpp
	Runtime/BenchMark.cpp
	Runtime/BenchMark.hpp
	Runtime/Platform/PlatformCommon.h
	Runtime/Platform/PlatformAndroid.h
	Runtime/Platform/PlatformWindows.h
	Runtime/Platform/PlatformLinux.h
)

set(src_files
	Options.cpp
	Options.hpp
)

set(src_files_magicalego
	MagicaLego/MagicaLegoGameInstance.hpp
	MagicaLego/MagicaLegoGameInstance.cpp
	MagicaLego/MagicaLegoUserInterface.hpp
	MagicaLego/MagicaLegoUserInterface.cpp
)
source_group("Assets" FILES ${src_files_assets})
source_group("Utilities" FILES ${src_files_utilities})
source_group("Vulkan" FILES ${src_files_vulkan})
source_group("Vulkan.RayTracing" FILES ${src_files_vulkan_raytracing})
source_group("Vulkan.ModernDeferred" FILES ${src_files_vulkan_moderndeferred})
source_group("Vulkan.LegacyDeferred" FILES ${src_files_vulkan_legacydeferred})
source_group("Vulkan.PipelineCommon" FILES ${src_files_common_compute_pipeline})
source_group("Vulkan.RayQuery" FILES ${src_files_vulkan_rayquery})
source_group("Vulkan.HybridDeferred" FILES ${src_files_vulkan_hybriddeferred})
source_group("ThirdParty.json11" FILES ${src_files_thirdparty_json11})
source_group("Editor" FILES ${src_files_editor})
source_group("Main" FILES ${src_files})
source_group("MagicaLego" FILES ${src_files_magicalego})



if ( ANDROID )
add_library(gkNextRenderer SHARED
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files_vulkan_rayquery}
	${src_files_vulkan_moderndeferred} 
	${src_files_vulkan_legacydeferred} 
	${src_files_vulkan_hybriddeferred}
	${src_files_common_compute_pipeline}
	${src_files_thirdparty_json11}
	${src_files_runtime}
	${src_files}
	AndroidMain.cpp
	GameActivitySources.cpp
)
else()
add_executable(gkNextRenderer
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files_vulkan_rayquery}
	${src_files_vulkan_moderndeferred} 
	${src_files_vulkan_legacydeferred} 
	${src_files_vulkan_hybriddeferred}
	${src_files_common_compute_pipeline}
	${src_files_thirdparty_json11}
	${src_files_runtime}
	${src_files}
	DesktopMain.cpp
)
add_executable(gkNextEditor
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files_vulkan_rayquery}
	${src_files_vulkan_moderndeferred} 
	${src_files_vulkan_legacydeferred} 
	${src_files_vulkan_hybriddeferred}
	${src_files_common_compute_pipeline}
	${src_files_thirdparty_json11}
	${src_files_editor}
	${src_files_runtime}
	${src_files}
	DesktopMain.cpp
)
add_executable(MagicaLego
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files_vulkan_rayquery}
	${src_files_vulkan_moderndeferred} 
	${src_files_vulkan_legacydeferred} 
	${src_files_vulkan_hybriddeferred}
	${src_files_common_compute_pipeline}
	${src_files_thirdparty_json11}
	${src_files_runtime}
	${src_files}
	${src_files_magicalego}
	DesktopMain.cpp
)
endif()


if (UNIX AND NOT APPLE AND NOT ANDROID)
	# GCC8 needs an extra lib for <filesystem>.
	# This is not needed with GCC9 or higher.
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()


# editor stuff
add_subdirectory(ThirdParty/ImNodeFlow)

if ( ANDROID )
set(AllTargets 
gkNextRenderer
)
else()
set(AllTargets 
gkNextRenderer 
gkNextEditor
MagicaLego 
)
endif()
# common setup
foreach(target IN LISTS AllTargets)
	# common dependencies
	add_dependencies(${target} Assets)
	set_target_properties(${target} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
	target_include_directories(${target} PRIVATE . ${STB_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
	target_include_directories(${target} PRIVATE ${CPP_BASE64_INCLUDE_DIRS})

	target_link_directories(${target} PRIVATE ${Vulkan_LIBRARY})

	if ( ANDROID )
	target_include_directories(${target} PRIVATE
		${ANDROID_NDK}/sources/android/native_app_glue)
	endif()

	if ( APPLE )
	target_compile_definitions(${target} PUBLIC BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)
	endif()

	if ( !ANDROID)
	target_link_libraries(${target} PRIVATE fmt::fmt rapidjsond)
	else()
	target_link_libraries(${target} PRIVATE fmt::fmt)
	endif()

	target_link_libraries(${target} PRIVATE CURL::libcurl Boost::boost Boost::exception Boost::program_options glm::glm imgui::imgui tinyobjloader::tinyobjloader draco::draco ${Vulkan_LIBRARIES} ${extra_libs})

	if ( ANDROID)
	target_link_libraries(${target} PRIVATE game-activity::game-activity log android)
	else()
	target_link_libraries(${target} PRIVATE glfw)
	endif()

	if (CMAKE_CXX_COMPILER MATCHES ".*mingw.*")
	target_link_libraries(${target} PRIVATE gdi32)
	endif()

	if ( WITH_AVIF )
	target_compile_definitions(${target} PUBLIC WITH_AVIF=1)
	target_compile_definitions(${target} PUBLIC AVIF_CODEC_AOM=SYSTEM)
	target_link_libraries(${target} PRIVATE avif)
	endif()

	if ( WITH_OIDN )
	target_compile_definitions(${target} PUBLIC WITH_OIDN=1)
	target_include_directories(${target} PRIVATE ../src/ThirdParty/oidn/include/)
	target_link_directories(${target} PRIVATE ../src/ThirdParty/oidn/lib/)
	target_link_libraries(${target} PRIVATE OpenImageDenoise OpenImageDenoise_core )

	add_custom_command(
				TARGET ${target} POST_BUILD
				COMMAND echo \"copy oidn dlls...\"
				COMMAND xcopy \"${CMAKE_CURRENT_SOURCE_DIR}/../src/ThirdParty/oidn/bin/*.dll\" \"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\" /Y /Q /F
			)
	endif()

	if( WIN32 )
	target_compile_definitions(${target} PUBLIC VK_USE_PLATFORM_WIN32_KHR PLATFORM__WINDOWS)
	endif()

	target_compile_definitions(${target} PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

	# warning as error
	if (MSVC)
	target_compile_options(${target} PRIVATE /WX)
	else()
	target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-but-set-variable -Wno-inconsistent-missing-override -Wno-unused-const-variable -Wno-unused-variable -Wno-unused-private-field -Wno-unused-function -Wno-sign-compare -Wno-unused-result)
	endif()

	# specific for targets
	if ( ${target} STREQUAL gkNextRenderer )
	target_compile_definitions(gkNextRenderer PUBLIC WITH_EDITOR=0)
	endif()

	if ( ${target} STREQUAL MagicaLego )
	target_compile_definitions(MagicaLego PUBLIC WITH_EDITOR=0 WITH_GAME=1)
	endif()

	if ( ${target} STREQUAL gkNextEditor )
	target_link_libraries(gkNextEditor PRIVATE ImNodeFlow)
	target_compile_definitions(gkNextEditor PUBLIC WITH_EDITOR=1)
	set_target_properties(ImNodeFlow PROPERTIES FOLDER "ThirdParty")

	# third-party warning fix
	if (MSVC)

	else()
	set_source_files_properties(${src_files_editor} PROPERTIES COMPILE_FLAGS "-w")
	endif()
	endif()

endforeach()

# set_target_properties(Assets PROPERTIES FOLDER "Assets")
# set_target_properties(${exe_name} PROPERTIES FOLDER "Runtime")
# set_target_properties(${exe_editor_name} PROPERTIES FOLDER "Editor")
