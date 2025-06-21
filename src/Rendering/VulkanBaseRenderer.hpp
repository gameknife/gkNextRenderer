#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/VulkanGpuTimer.hpp"
#include "Vulkan/Image.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Scene.hpp"
#include <vector>
#include <memory>
#include <cassert>
#include <chrono>
#include <functional>
#include <map>

namespace Vulkan
{
	namespace PipelineCommon
	{
		class BufferClearPipeline;
		class SoftwareGPULightBakePipeline;
		class SimpleComposePipeline;
		class GPUCullPipeline;
		class VisualDebuggerPipeline;
		class VisibilityPipeline;
		class GraphicsPipeline;
	}

	class RenderImage;
	class DescriptorSetManager;
}

namespace Assets
{
	class GlobalTexturePool;
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan 
{
	enum ERendererType
	{
		ERT_PathTracing,
		ERT_Hybrid,
		ERT_ModernDeferred,
		ERT_LegacyDeferred,
		ERT_VoxelTracing,
	};
		
	class VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(VulkanBaseRenderer)

		virtual ~VulkanBaseRenderer();

		const std::vector<VkExtensionProperties>& Extensions() const;
		const std::vector<VkLayerProperties>& Layers() const;
		const std::vector<VkPhysicalDevice>& PhysicalDevices() const;

		const class SwapChain& SwapChain() const { return *swapChain_; }
		class Window& Window() { return *window_; }
		const class Window& Window() const { return *window_; }

		bool HasSwapChain() const { return swapChain_.operator bool(); }

		void SetPhysicalDevice(VkPhysicalDevice physicalDevice);
		
		void Start();
		void End();
		
		void CaptureScreenShot();
		void CaptureEditorViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		
		RenderImage& GetRenderImage() const {return *rtEditorViewport_;}

		virtual void DrawFrame();


		VulkanBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers);

		const class Device& Device() const { return *device_; }
		class CommandPool& CommandPool() { return *commandPool_; }
		const class DepthBuffer& DepthBuffer() const { return *depthBuffer_; }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return uniformBuffers_; }
		const bool CheckerboxRendering() {return checkerboxRendering_;}
		class VulkanGpuTimer* GpuTimer() const {return gpuTimer_.get();}
		
		Assets::Scene& GetScene();
		void SetScene(std::shared_ptr<Assets::Scene> scene);
		virtual Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const;

		int FrameCount() const {return frameCount_;}

		virtual void SetPhysicalDeviceImpl(
			VkPhysicalDevice physicalDevice, 
			std::vector<const char*>& requiredExtensions, 
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures);
		
		virtual void OnDeviceSet();
		void CreateRenderImages();
		virtual void CreateSwapChain();
		virtual void DeleteSwapChain();
		virtual void PreRender(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		virtual void PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);

		virtual void BeforeNextFrame();

		virtual void AfterRenderCmd() {}
		virtual void AfterPresent() {}
		virtual void AfterUpdateScene() {}

		virtual void OnPreLoadScene() {}
		virtual void OnPostLoadScene() {}

		void InitializeBarriers(VkCommandBuffer commandBuffer);
		
		bool VisualDebug() const {return visualDebug_;}

		virtual void RegisterLogicRenderer(ERendererType type);
		virtual void SwitchLogicRenderer(ERendererType type);

		ERendererType CurrentLogicRendererType() const
		{
			return currentLogicRenderer_;
		}

		DescriptorSetManager& GetRTDescriptorSetManager() const
		{
			return *rtDescriptorSetManager_;
		}
		
		// Callbacks
		std::function<void()> DelegateOnDeviceSet;
		std::function<void()> DelegateCreateSwapChain;
		std::function<void()> DelegateDeleteSwapChain;
		std::function<void()> DelegateBeforeNextTick;
		std::function<Assets::UniformBufferObject(VkOffset2D, VkExtent2D)> DelegateGetUniformBufferObject;
		std::function<void(VkCommandBuffer, uint32_t)> DelegatePostRender;

		DeviceMemory* GetScreenShotMemory() const {return screenShotImageMemory_.get();}
	
		std::weak_ptr<Assets::Scene> scene_;
		
		bool checkerboxRendering_{};
		bool supportRayTracing_ {};
		bool supportDenoiser_ {};
		bool showWireframe_ {};
		int frameCount_{};
		bool forceSDR_{};
		bool visualDebug_{};

		// this texture could pass to global scope, it may contained by base renderer
		std::unique_ptr<RenderImage> rtDenoised;
		std::unique_ptr<RenderImage> rtOutput;
		std::unique_ptr<RenderImage> rtAccumlation;
		std::unique_ptr<RenderImage> rtVisibility;
		std::unique_ptr<RenderImage> rtObject0;
		std::unique_ptr<RenderImage> rtObject1;
		std::unique_ptr<RenderImage> rtPrevDepth;
		std::unique_ptr<RenderImage> rtMotionVector_;
		std::unique_ptr<RenderImage> rtAlbedo_;
		std::unique_ptr<RenderImage> rtNormal_;
		std::unique_ptr<RenderImage> rtShaderTimer_;
			
	protected:
		Assets::UniformBufferObject lastUBO;
	
	private:

		void UpdateUniformBuffer(uint32_t imageIndex);
		void RecreateSwapChain();

		const VkPresentModeKHR presentMode_;

		std::map< ERendererType, std::unique_ptr<class LogicRendererBase> > logicRenderers_;
		ERendererType currentLogicRenderer_;
		
		class Window* window_;
		std::unique_ptr<class Instance> instance_;
		std::unique_ptr<class DebugUtilsMessenger> debugUtilsMessenger_;
		std::unique_ptr<class Surface> surface_;
		std::unique_ptr<class Device> device_;
		std::unique_ptr<class SwapChain> swapChain_;
		
		std::vector<Assets::UniformBuffer> uniformBuffers_;
		
		std::unique_ptr<PipelineCommon::GraphicsPipeline> wireframePipeline_;
		std::unique_ptr<PipelineCommon::BufferClearPipeline> bufferClearPipeline_;
		std::unique_ptr<PipelineCommon::SimpleComposePipeline> simpleComposePipeline_;
		std::unique_ptr<PipelineCommon::VisualDebuggerPipeline> visualDebuggerPipeline_;
		std::unique_ptr<PipelineCommon::VisibilityPipeline> visibilityPipeline_;
		
		std::unique_ptr<class DepthBuffer> depthBuffer_;
		std::unique_ptr<FrameBuffer> visibilityFrameBuffer_;
		std::vector<FrameBuffer> swapChainFramebuffers_;
		
		
		std::unique_ptr<class CommandPool> commandPool_;
		std::unique_ptr<class CommandPool> commandPool2_;
		std::unique_ptr<class CommandBuffers> commandBuffers_;
		
		std::vector<class Semaphore> imageAvailableSemaphores_;
		std::vector<class Semaphore> renderFinishedSemaphores_;
		std::vector<class Fence> inFlightFences_;

		std::unique_ptr<PipelineCommon::SoftwareGPULightBakePipeline> softAmbientCubeGenPipeline_;
		std::unique_ptr<PipelineCommon::GPUCullPipeline> gpuCullPipeline_;
		
		std::unique_ptr<Image> screenShotImage_;
		std::unique_ptr<DeviceMemory> screenShotImageMemory_;
		std::unique_ptr<ImageView> screenShotImageView_;
		std::unique_ptr<RenderImage> rtEditorViewport_;

		std::unique_ptr<VulkanGpuTimer> gpuTimer_;
		std::unique_ptr<Assets::GlobalTexturePool> globalTexturePool_;
		std::unique_ptr<Vulkan::DescriptorSetManager> rtDescriptorSetManager_;

		uint32_t currentImageIndex_{};
		size_t currentFrame_{};
		Fence* currentFence;

		uint64_t uptime {};
	};
	
	class LogicRendererBase
	{
	public:
		LogicRendererBase( VulkanBaseRenderer& baseRender );
		virtual ~LogicRendererBase() {};

		virtual void OnDeviceSet() {};
		virtual void CreateSwapChain(const VkExtent2D& extent) {};
		virtual void DeleteSwapChain() {};
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) {};
		virtual void BeforeNextFrame() {};
		
		VulkanBaseRenderer& baseRender_;
		template<typename T>
		T& GetBaseRender() { return static_cast<T&>(baseRender_); }

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
	};

}
