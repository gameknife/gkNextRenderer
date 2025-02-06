#pragma once

#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp"
#include "RayTracingProperties.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
	}

	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
}

namespace Vulkan::RayTracing
{
	class RayQueryPipeline;
	class RayTraceBaseRenderer;
	class LogicRendererBase;
	
	class RayTraceBaseRenderer : public Vulkan::VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(RayTraceBaseRenderer);

		RayTraceBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers);
		virtual ~RayTraceBaseRenderer();
				
		std::vector<TopLevelAccelerationStructure>& TLAS() { return topAs_; }
		std::vector<BottomLevelAccelerationStructure>& BLAS() { return bottomAs_; }
		Buffer& AmbientCubeBuffer() { return *ambientCubeBuffer_; }

	protected:
		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures) override;
		
		void OnDeviceSet() override;
		void CreateAccelerationStructures();
		void DeleteAccelerationStructures();
		void CreateSwapChain() override;
		void DeleteSwapChain() override;

		virtual void AfterRenderCmd() override;
		virtual void BeforeNextFrame() override;
		virtual void AfterUpdateScene() override;

		virtual void OnPreLoadScene() override;
		virtual void OnPostLoadScene() override;

		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		
		virtual void SetRaycastRay(glm::vec3 org, glm::vec3 dir, std::function<bool(Assets::RayCastResult)> callback) override;


	protected:
		void CreateBottomLevelStructures(VkCommandBuffer commandBuffer);
		void CreateTopLevelStructures(VkCommandBuffer commandBuffer);
		
		std::unique_ptr<class RayTracingProperties> rayTracingProperties_;
	
		std::vector<BottomLevelAccelerationStructure> bottomAs_;
		std::unique_ptr<Buffer> bottomBuffer_;
		std::unique_ptr<DeviceMemory> bottomBufferMemory_;
		std::unique_ptr<Buffer> bottomScratchBuffer_;
		std::unique_ptr<DeviceMemory> bottomScratchBufferMemory_;
		std::vector<TopLevelAccelerationStructure> topAs_;
		std::unique_ptr<Buffer> topBuffer_;
		std::unique_ptr<DeviceMemory> topBufferMemory_;
		std::unique_ptr<Buffer> topScratchBuffer_;
		std::unique_ptr<DeviceMemory> topScratchBufferMemory_;
		std::unique_ptr<Buffer> instancesBuffer_;
		std::unique_ptr<DeviceMemory> instancesBufferMemory_;
		
		std::unique_ptr<Assets::RayCastBuffer> rayCastBuffer_;
		std::unique_ptr<PipelineCommon::RayCastPipeline> raycastPipeline_;
		std::unique_ptr<Buffer> ambientCubeBuffer_;
		std::unique_ptr<DeviceMemory> ambientCubeBufferMemory_;
		std::unique_ptr<PipelineCommon::AmbientGenPipeline> ambientGenPipeline_;

		std::vector<Assets::RayCastRequest> rayRequested_;
		std::vector<Assets::RayCastRequest> rayFetched_;

		int tlasUpdateRequest_ {};
	};
}
