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

		void RegisterLogicRenderer(ERendererType type) override;
		void SwitchLogicRenderer(ERendererType type) override;
		
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

		virtual void OnPreLoadScene() override;
		virtual void OnPostLoadScene() override;

		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

		virtual bool GetFocusDistance(float& distance) const override;
		virtual bool GetLastRaycastResult(Assets::RayCastResult& result) const override;
		virtual void SetRaycastRay(glm::vec3 org, glm::vec3 dir) const override;


	protected:
		void CreateBottomLevelStructures(VkCommandBuffer commandBuffer);
		void CreateTopLevelStructures(VkCommandBuffer commandBuffer);

		std::vector< std::unique_ptr<LogicRendererBase> > logicRenderers_;
		ERendererType currentLogicRenderer_;
		
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
		
		Assets::RayCastResult cameraCenterCastResult_;
		mutable Assets::RayCastContext cameraCenterCastContext_;
		std::unique_ptr<Assets::RayCastBuffer> rayCastBuffer_;
		std::unique_ptr<PipelineCommon::RayCastPipeline> raycastPipeline_;
	};

	class LogicRendererBase
	{
	public:
		LogicRendererBase( RayTracing::RayTraceBaseRenderer& baseRender );
		virtual ~LogicRendererBase() {};
		
		virtual void CreateSwapChain() {};
		virtual void DeleteSwapChain() {};
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) {};
		
		RayTracing::RayTraceBaseRenderer& baseRender_;

		const class SwapChain& SwapChain() const { return baseRender_.SwapChain(); }
		class Window& Window() { return baseRender_.Window(); }
		
		const class Device& Device() const { return baseRender_.Device(); }
		class CommandPool& CommandPool() { return baseRender_.CommandPool(); }
		const class DepthBuffer& DepthBuffer() const { return baseRender_.DepthBuffer(); }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return baseRender_.UniformBuffers(); }
		class VulkanGpuTimer* GpuTimer() const {return baseRender_.GpuTimer();}
		
		const Assets::Scene& GetScene() {return baseRender_.GetScene();}

		int FrameCount() const {return baseRender_.FrameCount();}

		bool VisualDebug() const {return baseRender_.VisualDebug();}

		std::vector<TopLevelAccelerationStructure>& TLAS() { return baseRender_.TLAS(); }
		std::vector<BottomLevelAccelerationStructure>& BLAS() { return baseRender_.BLAS(); }
	};

}
