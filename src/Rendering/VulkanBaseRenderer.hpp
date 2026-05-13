#pragma once

#include "Vulkan/RenderingPipeline.hpp"
#include "Vulkan/SyncAndTiming.hpp"
#include "Vulkan/GpuResources.hpp"
#include "Assets/GPU/UniformBuffer.hpp"
#include "Assets/Core/Scene.hpp"
#include <vector>
#include <memory>
#include <cassert>
#include <functional>
#include <map>

namespace StreamlineWrapper
{
	bool ShouldInitialize();
	void Initialize();
	void LazyInit(VkDevice device, VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t computeQueueIdx, uint32_t computeQueueFamily, uint32_t graphicsQueueIdx, uint32_t graphicsQueueFamily, bool& outSupportDLSS, bool& outSupportDLSSRR);
	void Shutdown();
}

namespace Vulkan
{
	namespace PipelineCommon
	{
		class VisibilityPipeline;
		class GraphicsPipeline;
		class ZeroBindPipeline;
		class ZeroBindCustomPushConstantPipeline;
	}

	class RenderImage;
	class DescriptorSetManager;
	class Instance;
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
		ERT_ModernDeferred,
		ERT_LegacyDeferred,
		ERT_VoxelTracing,
		ERT_LegacyDeferredNoAmbient,
	};

	inline bool RendererUsesAmbientCube(const ERendererType type)
	{
		return type != ERT_LegacyDeferredNoAmbient;
	}
		
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

		virtual void DrawFrame();


		VulkanBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers, Instance* instance);

		const class Device& Device() const { return *device_; }
		class CommandPool& CommandPool() { return *commandPool_; }
		const class DepthBuffer& DepthBuffer() const { return *depthBuffer_; }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return uniformBuffers_; }
		const bool CheckerboxRendering() {return checkerboxRendering_;}
		class VulkanGpuTimer* GpuTimer() const {return gpuTimer_.get();}
		
		Assets::Scene& GetScene();
		void SetScene(std::shared_ptr<Assets::Scene> scene);
		virtual Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const;
		bool SupportsRayTracing() const { return supportRayTracing_; }

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
		void ReloadShaders();
		void RequestRecreateSwapChain() { requestRecreateSwapChain_ = true; }
		void RequestClearAmbientCubeCache() { requestClearAmbientCubeCache_ = true; }
		virtual void PreRender(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		virtual void PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);

		virtual void BeforeNextFrame();

		virtual void AfterRenderCmd() {}
		virtual void AfterPresent() {}
		virtual void AfterUpdateScene() {}

		virtual void OnPreLoadScene() {}
		virtual void OnPostLoadScene() { skinModelUpdateRequests_.clear(); }

		void InitializeBarriers(VkCommandBuffer commandBuffer);
		
		void UpdateStreamline(VkCommandBuffer commandBuffer, uint32_t imageIndex);
	    
	    void RequestSkinUpdate(uint32_t modelId) { skinModelUpdateRequests_.push_back(modelId); }

		bool VisualDebug() const {return visualDebug_;}
		
		bool SupportDLSS() const { return supportDLSS_; }
		bool SupportDLSSRR() const { return supportDLSSRR_; }
		bool HasFullAmbientCubeBudget() const { return fullAmbientCubeBudget_; }
		bool CurrentRendererUsesAmbientCube() const { return RendererUsesAmbientCube(currentLogicRenderer_); }

		virtual void RegisterLogicRenderer(ERendererType type);
		virtual void SwitchLogicRenderer(ERendererType type);

		ERendererType CurrentLogicRendererType() const
		{
			return currentLogicRenderer_;
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
		bool supportDLSS_{};
		bool supportDLSSRR_{};
		bool supportDenoiser_ {};
		bool streamlineDeviceExtensionsEnabled_{};
		bool fullAmbientCubeBudget_{true};
		// bool showWireframe_ {};
		int frameCount_{};
		bool forceSDR_{};
		bool visualDebug_{};

		void CreateStorageImage(uint32_t bindlessIdx, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName);
		const RenderImage* GetStorageImage(uint32_t bindlessIdx) const;
		uint32_t GetTemporalStorageImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName);

		void CaptureOIDN(VkCommandBuffer commandBuffer);

	protected:
		Assets::UniformBufferObject lastUBO;
		std::vector<std::unique_ptr<RenderImage> > bindlessStorageImages_;
		uint32_t tempStorageImageCreated_ {};

		void UpdateSkinningBuffers();
		void ClearAmbientCubeCache(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void BakeAmbientCubePropagation(VkCommandBuffer commandBuffer, uint32_t imageIndex);
	    
	    std::vector<uint32_t> skinModelUpdateRequests_;

		std::unique_ptr<Buffer> skinnedVertexBuffer_;
		std::unique_ptr<DeviceMemory> skinnedVertexBufferMemory_;

		std::unique_ptr<Buffer> jointMatricesBuffer_;
		std::unique_ptr<DeviceMemory> jointMatricesBufferMemory_;

		uint32_t currentSkinnedVertexBufferSize_{};
		uint32_t currentJointMatrixBufferSize_{};

	private:
		void RecreateSwapChain();
		void UpdateUniformBuffer(uint32_t imageIndex);

		const VkPresentModeKHR presentMode_;
		bool requestRecreateSwapChain_ = false;
		bool requestClearAmbientCubeCache_ = true;
		bool ambientCubePropagationStateInitialized_ = false;
		bool lastAmbientCubePropagation_ = false;

		std::map< ERendererType, std::unique_ptr<class LogicRendererBase> > logicRenderers_;
		ERendererType currentLogicRenderer_;
		
		class Window* window_;
		std::unique_ptr<class Instance> instance_;
		std::unique_ptr<class DebugUtilsMessenger> debugUtilsMessenger_;
		std::unique_ptr<class Surface> surface_;
		std::unique_ptr<class Device> device_;
		std::unique_ptr<class SwapChain> swapChain_;
		
		std::vector<Assets::UniformBuffer> uniformBuffers_;
		
		//std::unique_ptr<PipelineCommon::GraphicsPipeline> wireframePipeline_;
		std::unique_ptr<PipelineCommon::VisibilityPipeline> visibilityPipeline_;
		
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> bufferClearPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> simpleComposePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> visualDebuggerPipeline_;
		
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> softAmbientCubeGenPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> clearAmbientCubeCachePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> propagationAmbientCubeGenPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> injectAmbientCubeGenPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> distanceFieldInitPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> distanceFieldJumpPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> distanceFieldResolvePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> gpuCullPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> skinningPipeline_;

		std::unique_ptr<class DepthBuffer> depthBuffer_;
		std::unique_ptr<FrameBuffer> visibilityFrameBuffer_;
		//std::unique_ptr<FrameBuffer> wireframeFramebuffer_;
		
		std::unique_ptr<class CommandPool> commandPool_;
		std::unique_ptr<class CommandPool> commandPool2_;
		std::unique_ptr<class CommandBuffers> commandBuffers_;
		
		std::vector<class Semaphore> imageAvailableSemaphores_;
		std::vector<class Semaphore> renderFinishedSemaphores_;
		std::vector<class Fence> inFlightFences_;

		std::unique_ptr<Image> screenShotImage_;
		std::unique_ptr<DeviceMemory> screenShotImageMemory_;
		std::unique_ptr<ImageView> screenShotImageView_;

		std::unique_ptr<VulkanGpuTimer> gpuTimer_;
		std::unique_ptr<Assets::GlobalTexturePool> globalTexturePool_;

		uint32_t currentImageIndex_{};
		size_t currentFrame_{};
		Fence* currentFence;
	};
	
	class LogicRendererBase
	{
	public:
		LogicRendererBase( VulkanBaseRenderer& baseRender ): baseRender_(baseRender) {}
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
