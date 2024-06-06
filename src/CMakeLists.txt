
set(exe_name ${MAIN_PROJECT})

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
	Utilities/Glm.hpp
	Utilities/StbImage.cpp
	Utilities/StbImage.hpp
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
	Vulkan/RayTracing/AccelerationStructure.cpp
	Vulkan/RayTracing/AccelerationStructure.hpp
	Vulkan/RayTracing/RayTracingRenderer.cpp
	Vulkan/RayTracing/RayTracingRenderer.hpp
	Vulkan/RayTracing/BottomLevelAccelerationStructure.cpp
	Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp
	Vulkan/RayTracing/BottomLevelGeometry.cpp
	Vulkan/RayTracing/BottomLevelGeometry.hpp
	Vulkan/RayTracing/DeviceProcedures.cpp
	Vulkan/RayTracing/DeviceProcedures.hpp
	Vulkan/RayTracing/RayTracingPipeline.cpp
	Vulkan/RayTracing/RayTracingPipeline.hpp
	Vulkan/RayTracing/RayTracingProperties.cpp
	Vulkan/RayTracing/RayTracingProperties.hpp
	Vulkan/RayTracing/ShaderBindingTable.cpp
	Vulkan/RayTracing/ShaderBindingTable.hpp
	Vulkan/RayTracing/TopLevelAccelerationStructure.cpp
	Vulkan/RayTracing/TopLevelAccelerationStructure.hpp
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

set(src_files_common_compute_pipeline
	Vulkan/PipelineCommon/CommonComputePipeline.cpp
	Vulkan/PipelineCommon/CommonComputePipeline.hpp
)

set(src_files_thirdparty_json11
	ThirdParty/json11/json11.cpp
	ThirdParty/json11/json11.hpp
)

set(src_files
	main.cpp
	ModelViewController.cpp
	ModelViewController.hpp
	Options.cpp
	Options.hpp
	Application.cpp
	Application.hpp
	SceneList.cpp
	SceneList.hpp
	UserInterface.cpp
	UserInterface.hpp
	UserSettings.hpp
)

set_source_files_properties(
  ${src_files_thirdparty_json11}
  PROPERTIES
  COMPILE_FLAGS "-w"
)

source_group("Assets" FILES ${src_files_assets})
source_group("Utilities" FILES ${src_files_utilities})
source_group("Vulkan" FILES ${src_files_vulkan})
source_group("Vulkan.RayTracing" FILES ${src_files_vulkan_raytracing})
source_group("Vulkan.ModernDeferred" FILES ${src_files_vulkan_moderndeferred})
source_group("Vulkan.LegacyDeferred" FILES ${src_files_vulkan_legacydeferred})
source_group("Vulkan.PipelineCommon" FILES ${src_files_common_compute_pipeline})
source_group("ThirdParty.json11" FILES ${src_files_thirdparty_json11})
source_group("Main" FILES ${src_files})




if ( ANDROID )
add_library(${exe_name} SHARED
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files_vulkan_moderndeferred} 
	${src_files_vulkan_legacydeferred} 
	${src_files_common_compute_pipeline}
	${src_files_thirdparty_json11}
	${src_files}
	GameActivitySources.cpp
)
else()
add_executable(${exe_name}
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files_vulkan_moderndeferred} 
	${src_files_vulkan_legacydeferred} 
	${src_files_common_compute_pipeline}
	${src_files_thirdparty_json11}
	${src_files}
)
endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	# GCC8 needs an extra lib for <filesystem>.
	# This is not needed with GCC9 or higher.
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

add_dependencies(${exe_name} Assets)
set_target_properties(${exe_name} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
target_include_directories(${exe_name} PRIVATE . ${STB_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})
target_include_directories(${exe_name} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
target_include_directories(${exe_name} PRIVATE ${CPP_BASE64_INCLUDE_DIRS})
target_link_directories(${exe_name} PRIVATE ${Vulkan_LIBRARY})

if ( ANDROID )
target_include_directories(${exe_name} PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue)
endif()

target_compile_definitions(${exe_name} PUBLIC AVIF_CODEC_AOM=SYSTEM)

if ( APPLE )
target_compile_definitions(${exe_name} PUBLIC BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)
endif()

if ( ANDROID )
target_link_libraries(${exe_name} PRIVATE game-activity::game-activity log android CURL::libcurl PRIVATE Boost::boost Boost::exception Boost::program_options glm::glm imgui::imgui tinyobjloader::tinyobjloader draco::draco ${Vulkan_LIBRARIES} ${extra_libs})
else()
target_link_libraries(${exe_name} PRIVATE CURL::libcurl PRIVATE Boost::boost Boost::exception Boost::program_options glfw glm::glm imgui::imgui tinyobjloader::tinyobjloader draco::draco ${Vulkan_LIBRARIES} ${extra_libs})
endif()

if ( WITH_AVIF )
target_link_libraries(${exe_name} PRIVATE avif)
endif()