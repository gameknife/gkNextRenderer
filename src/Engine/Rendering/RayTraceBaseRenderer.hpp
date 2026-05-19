#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Engine/Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class ZeroBindWithTLASPipeline;
	}

	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
}

namespace Vulkan::RayTracing
{
	class RayTraceBaseRenderer : public Vulkan::VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(RayTraceBaseRenderer);

		RayTraceBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers, Instance* instance);
		virtual ~RayTraceBaseRenderer();
				
		std::vector<TopLevelAccelerationStructure>& TLAS() { return topAs_; }
		std::vector<BottomLevelAccelerationStructure>& BLAS() { return bottomAs_; }

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

		virtual void PreRender(VkCommandBuffer commandBuffer, const uint32_t imageIndex) override;
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		virtual void PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
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
		
		std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> directLightGenPipeline_;
	    
		int tlasUpdateRequest_ {};
	};
}
