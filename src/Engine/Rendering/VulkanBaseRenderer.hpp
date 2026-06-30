#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Vulkan/VulkanVideoCaps.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Engine/Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include <vector>
#include <memory>
#include <cassert>
#include <functional>
#include <map>
#include <array>
#include <set>

namespace Rendering::Upscaler
{
	class IUpscaler;
}

namespace Vulkan::GaussianSplat
{
    class GaussianSplatPass;
}

namespace Vulkan
{
	class AssetThumbnailRenderer;
	class FActiveRenderViewScope;
	class LogicRendererBase;
	class MaterialPreviewRenderer;
	class OffscreenRenderViewController;
	class ReferenceRenderViewController;
	class RenderViewResourceFactory;
	class RenderViewServices;

	enum ERendererType
	{
		ERT_PathTracing,
		ERT_SoftwareTracing,
		ERT_SoftwareModern,
		ERT_VoxelTracing,
		ERT_SoftwareModernNoAmbient,
	};

	struct FRendererRequirements
	{
		bool requestAmbientCube = false;
		bool requestRayTracing = false;
		// Needs voxel SDF geometry (matId + per-axis distance field) but not necessarily ambient cubes.
		bool requestVoxelGeometry = false;

		void Merge(const FRendererRequirements& other)
		{
			requestAmbientCube = requestAmbientCube || other.requestAmbientCube;
			requestRayTracing = requestRayTracing || other.requestRayTracing;
			requestVoxelGeometry = requestVoxelGeometry || other.requestVoxelGeometry;
		}

		// Ambient cube baking implies voxel geometry (the cube bake reads/writes the voxel SDF),
		// so any cube-requesting renderer also needs voxel geometry available.
		bool NeedsVoxelGeometry() const { return requestAmbientCube || requestVoxelGeometry; }
	};

	FRendererRequirements GetRendererRequirements(ERendererType type);
	const char* GetRendererName(ERendererType type);

	struct FReferenceViewLayout
	{
		const char* debugName = "";
		uint32_t column = 0;
		uint32_t row = 0;
	};

	FReferenceViewLayout GetReferenceViewLayout(ERendererType type);
		
	class VulkanBaseRenderer
	{
	public:
		VULKAN_NON_COPIABLE(VulkanBaseRenderer)

		VulkanBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers, Instance* instance);
		virtual ~VulkanBaseRenderer();

		// Lifecycle
		void Start();
		void End();
		void DrawFrame();
		void ReloadShaders();
		void RequestRecreateSwapChain() { requestRecreateSwapChain_ = true; }
		VkPresentModeKHR RequestedPresentMode() const { return presentMode_; }
		void SetRequestedPresentMode(VkPresentModeKHR presentMode)
		{
			if (presentMode_ != presentMode)
			{
				presentMode_ = presentMode;
				RequestRecreateSwapChain();
			}
		}

		// Device / swapchain access
		const class Device& Device() const { return *ctx_.device; }
		class CommandPool& CommandPool() { return *ctx_.commandPool; }
		const class SwapChain& SwapChain() const { return *frame_.swapChain; }
		class Window& Window() { return *ctx_.window; }
		const class Window& Window() const { return *ctx_.window; }
		const class DepthBuffer& DepthBuffer() const { return *frame_.depthBuffer; }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return frame_.uniformBuffers; }
		class VulkanGpuTimer* GpuTimer() const {return ctx_.gpuTimer.get();}
		bool HasSwapChain() const { return frame_.swapChain.operator bool(); }
		int FrameCount() const {return frame_.frameCount;}
		uint64_t RecordingSubmitSerial() const { return frame_.recordingSubmitSerial; }
		uint64_t CompletedSubmitSerial() const { return frame_.completedSubmitSerial; }
		DeviceMemory* GetScreenShotMemory() const {return screenshot_.imageMemory.get();}
		const Image* GetScreenShotImage() const { return screenshot_.image.get(); }

		// Scene
		Assets::Scene& GetScene();
		void SetScene(std::shared_ptr<Assets::Scene> scene);
		Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const;
		void OnPreLoadScene();
		void OnPostLoadScene();
		void RefreshSceneSwapChainResources();
		void CreateSwapChain();
		void DeleteSwapChain();

		// Multi-viewport (RenderView): primary plus persistent/offscreen/transient views.
		RenderViewManager& RenderViews() { return *renderViews_; }
		const RenderViewManager& RenderViews() const { return *renderViews_; }
		RenderViewServices& ViewServices() { return *renderViewServices_; }
		const RenderViewServices& ViewServices() const { return *renderViewServices_; }
		RenderView& PrimaryView() { return renderViews_->Primary(); }
		const RenderView& PrimaryView() const { return renderViews_->Primary(); }
		RenderView& ActiveRenderView() { return activeRenderView_ != nullptr ? *activeRenderView_ : PrimaryView(); }
		const RenderView& ActiveRenderView() const { return activeRenderView_ != nullptr ? *activeRenderView_ : PrimaryView(); }
		FViewRenderState& PrimaryViewState() { return renderViews_->Primary().State(); }
		const FViewRenderState& PrimaryViewState() const { return renderViews_->Primary().State(); }

		// RT bank base of the view currently being recorded; injected into GPUScene.custom_data_0
		// so shaders resolve screen-space RT slots through Bindless::ViewRT. 0 == primary view.
		uint32_t ActiveViewBankBase() const { return activeViewBankBase_; }
		void SetActiveViewBankBase(uint32_t bankBase) { activeViewBankBase_ = bankBase; }

		// Camera UBO device address of the view currently being recorded -> GPUScene.Camera.
		// 0 == primary view (uses the per-image uniform buffer).
		VkDeviceAddress ActiveViewCameraAddress(uint32_t imageIndex) const;
		void SetActiveViewCameraAddress(VkDeviceAddress address) { activeViewCameraAddress_ = address; }
		VkExtent2D ActiveViewRenderExtent() const;
		void SetActiveViewRenderExtent(VkExtent2D extent) { activeViewRenderExtent_ = extent; }

		// Renderer registry
		void RegisterLogicRenderer(ERendererType type);
		void SwitchLogicRenderer(ERendererType type);
		ERendererType CurrentLogicRendererType() const { return logicRenderers_.current; }
		FRendererRequirements CurrentRendererRequirements() const;
		FRendererRequirements RegisteredRendererRequirements() const;
		bool ShouldSkipAmbientCubeUpdates() const;

		// Capabilities / flags
		bool VisualDebug() const {return visualDebug_;}
		bool SupportsRayTracing() const { return caps_.supportRayTracing; }
		bool SupportDLSS() const { return caps_.supportDLSS; }
		bool SupportDLSSRR() const { return caps_.supportDLSSRR; }
		bool SupportDLSSG() const { return caps_.supportDLSSG; }
		bool SupportReflex() const { return caps_.supportReflex; }
		Rendering::Upscaler::FFrameGenerationState GetFrameGenerationState() const;
		bool HasFullAmbientCubeBudget() const { return caps_.fullAmbientCubeBudget; }
		void SetDenoiserEnabled(bool enabled) { caps_.supportDenoiser = enabled; }
		void SetVisualDebugEnabled(bool enabled) { visualDebug_ = enabled; }
		const FVulkanVideoCaps& VideoCaps() const { return videoCaps_; }
		void QueueSubmitSignalSemaphore(VkSemaphore semaphore, uint64_t value = 0);

		// Engine callbacks
		struct Delegates
		{
			std::function<void()> onDeviceSet;
			std::function<void()> createSwapChain;
			std::function<void()> deleteSwapChain;
			std::function<void()> beforeNextTick;
			std::function<Assets::UniformBufferObject(VkOffset2D, VkExtent2D)> getUniformBufferObject;
			std::function<void(VkCommandBuffer, uint32_t)> postRender;
		};
		Delegates& GetDelegates() { return delegates_; }
		const Delegates& GetDelegates() const { return delegates_; }

		// Shared render resources used by logic renderers
		void CaptureScreenShot();
		const RenderImage* GetStorageImage(uint32_t bindlessIdx) const;
		// Screen-space RT image of the view currently being recorded (its bank). C++ counterpart
		// of the shader-side Bindless::GetViewStorageTexture; resolves slot through the active
		// view's bank base. Primary view (base 0) == GetStorageImage (legacy absolute).
		const RenderImage* GetViewStorageImage(uint32_t slot) const { return GetStorageImage(activeViewBankBase_ + slot); }
		void InitializeBarriers(VkCommandBuffer commandBuffer);
		void RequestSkinUpdate(uint32_t modelId) { skin_.updateRequests.push_back(modelId); }
		std::vector<RayTracing::TopLevelAccelerationStructure>& TLAS();

	private:
		friend class AssetThumbnailRenderer;
		friend class FActiveRenderViewScope;
		friend class MaterialPreviewRenderer;
		friend class OffscreenRenderViewController;
		friend class ReferenceRenderViewController;
		friend class RenderViewResourceFactory;

		// Internal resource groups
		struct DeviceCaps
		{
			bool supportRayTracing       = false;
			bool supportDLSS             = false;
			bool supportDLSSRR           = false;
			bool supportDLSSG            = false;
			bool supportReflex           = false;
			bool supportPCL              = false;
			bool supportDenoiser         = false;
			bool streamlineExtsEnabled   = false;
			bool fullAmbientCubeBudget   = true;
			bool supportSubgroupCull     = false;
		};

		struct RayTracingResources
		{
			std::unique_ptr<RayTracing::RayTracingProperties> properties;

			std::vector<RayTracing::BottomLevelAccelerationStructure> blas;
			std::unique_ptr<Buffer> blasBuffer;
			std::unique_ptr<DeviceMemory> blasMemory;
			std::unique_ptr<Buffer> blasScratch;
			std::unique_ptr<DeviceMemory> blasScratchMemory;

			std::vector<RayTracing::TopLevelAccelerationStructure> tlas;
			std::unique_ptr<Buffer> tlasBuffer;
			std::unique_ptr<DeviceMemory> tlasMemory;
			std::unique_ptr<Buffer> tlasScratch;
			std::unique_ptr<DeviceMemory> tlasScratchMemory;

			std::unique_ptr<Buffer> instancesBuffer;
			std::unique_ptr<DeviceMemory> instancesMemory;

			std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> directLightGenPipeline;

			int tlasUpdateRequest = 0;
		};

		struct AmbientCubePipelines
		{
			std::unique_ptr<PipelineCommon::ZeroBindPipeline> softBake;
			std::unique_ptr<PipelineCommon::ZeroBindPipeline> clearCache;
			bool requestClearCache = true;
		};

		struct SkinnedMeshResources
		{
			std::unique_ptr<Buffer> vertexBuffer;
			std::unique_ptr<DeviceMemory> vertexMemory;
			std::unique_ptr<Buffer> jointBuffer;
			std::unique_ptr<DeviceMemory> jointMemory;
			uint32_t vertexBufferSize = 0;
			uint32_t jointBufferSize = 0;
			std::vector<uint32_t> updateRequests;
			std::unique_ptr<PipelineCommon::ZeroBindPipeline> pipeline;
		};

		struct DeviceContext
		{
			class Window* window = nullptr;
			std::unique_ptr<class Instance> instance;
			std::unique_ptr<class DebugUtilsMessenger> debugUtilsMessenger;
			std::unique_ptr<class Surface> surface;
			std::unique_ptr<class Device> device;
			std::unique_ptr<class CommandPool> commandPool;
			std::unique_ptr<class CommandPool> commandPool2;
			std::unique_ptr<class VulkanGpuTimer> gpuTimer;
			std::unique_ptr<Assets::GlobalTexturePool> globalTexturePool;
		};

		struct FrameResources
		{
			std::unique_ptr<class SwapChain> swapChain;
			std::unique_ptr<class DepthBuffer> depthBuffer;
			std::unique_ptr<class CommandBuffers> commandBuffers;
			std::vector<class Semaphore> imageAvailableSemaphores;
			std::vector<class Semaphore> renderFinishedSemaphores;
			std::vector<class Fence> inFlightFences;
			std::vector<Assets::UniformBuffer> uniformBuffers;
			uint32_t currentImageIndex = 0;
			size_t currentFrame = 0;
			Fence* currentFence = nullptr;
			uint64_t currentFenceSerial = 0;
			uint64_t completedSubmitSerial = 0;
			uint64_t recordingSubmitSerial = 0;
			uint64_t nextSubmitSerial = 1;
			std::vector<uint64_t> inFlightFenceSubmitSerials;
			std::vector<VkSemaphore> queuedSignalSemaphores;
			std::vector<uint64_t> queuedSignalValues;
			int frameCount = 0;
			Assets::UniformBufferObject lastUBO;
			Rendering::Upscaler::FFrameToken streamlineFrameToken;
		};

		struct BindlessStorageImages
		{
			std::vector<std::unique_ptr<RenderImage> > images;
		};

		struct OverlayPipelines
		{
			std::unique_ptr<PipelineCommon::GraphicsPipeline> wireframePipeline;
			std::unique_ptr<PipelineCommon::VisibilityPipeline> visibilityPipeline;
			std::unique_ptr<Shadow::ShadowMapPass> sunShadowPass;
			std::unique_ptr<GaussianSplat::GaussianSplatPass> gaussianSplatPass;
			std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> bufferClearPipeline;
			std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> simpleComposePipeline;
			std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> visualDebuggerPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> gpuCullCompactPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> softMeshShaderFinalizePipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> softMeshShaderExpandPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> shadowGpuCullCompactPipeline;
			std::unique_ptr<FrameBuffer> visibilityFrameBuffer;
			std::vector<FrameBuffer> wireframeFrameBuffers;
		};

		struct LogicRendererRegistry
		{
			std::vector<ERendererType> registeredTypes;
			std::map< ERendererType, std::unique_ptr<class LogicRendererBase> > renderers;
			std::set<ERendererType> swapChainCreatedTypes;
			ERendererType current = ERT_SoftwareModern;
		};

		struct ScreenshotResources
		{
			std::unique_ptr<Image> image;
			std::unique_ptr<DeviceMemory> imageMemory;
			std::unique_ptr<ImageView> imageView;
		};

		struct FrameGenerationResources
		{
			std::vector<std::unique_ptr<RenderImage>> hudlessImages;
		};

		DeviceCaps caps_;
		FVulkanVideoCaps videoCaps_;
		DeviceContext ctx_;
		FrameResources frame_;
		SkinnedMeshResources skin_;
		BindlessStorageImages bindless_;
		std::unique_ptr<RayTracingResources> rt_;
		ScreenshotResources screenshot_;
		FrameGenerationResources frameGeneration_;
		AmbientCubePipelines ambient_;
		OverlayPipelines overlay_;
		LogicRendererRegistry logicRenderers_;
		std::unique_ptr<RenderViewManager> renderViews_ = std::make_unique<RenderViewManager>();
		std::unique_ptr<RenderViewServices> renderViewServices_;
		std::unique_ptr<ReferenceRenderViewController> referenceViewController_;
		Assets::Scene* activeSceneOverride_ = nullptr;
		RenderView* activeRenderView_ = nullptr;
		uint32_t activeViewBankBase_ = 0;
		VkExtent2D activeViewRenderExtent_{0, 0};
		VkDeviceAddress activeViewCameraAddress_ = 0;
		// Visibility framebuffer for the view currently being recorded (null => primary/bank-0).
		FrameBuffer* activeVisibilityFrameBuffer_ = nullptr;
		Delegates delegates_;
		std::unique_ptr<Rendering::Upscaler::IUpscaler> upscaler_;

		std::weak_ptr<Assets::Scene> scene_;
		VkPresentModeKHR presentMode_;
		bool checkerboxRendering_{};
		bool forceSDR_{};
		bool visualDebug_{};
		bool requestRecreateSwapChain_ = false;
		bool resetUpscalerHistory_ = true;

		// Device / swapchain internals
		void SelectPhysicalDevice(uint32_t gpuIdx);
		void SetPhysicalDevice(VkPhysicalDevice physicalDevice);
		void SetPhysicalDeviceImpl(
			VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures);
		void OnDeviceSet();
		LogicRendererBase* EnsureLogicRenderer(ERendererType type);
		bool IsLogicRendererRegistered(ERendererType type) const;
		void EnsureLogicRendererSwapChain(ERendererType type, LogicRendererBase& logicRenderer);
		void CreateRenderImages();
		// Creates the full screen-space RT set at [bankBase + RT_X]. bankBase 0 == primary view.
		void CreateRenderTargetBank(uint32_t bankBase);
		void CreateRenderTargetBank(uint32_t bankBase, VkExtent2D extent);
		void ScheduleRenderView(RenderView& view,
		                        LogicRendererBase& logicRenderer,
		                        bool clearSwapchain,
		                        FRenderViewPostCallback postRender = {});
		bool DispatchScheduledRenderViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void SetRenderViewUbo(RenderView& view, uint32_t imageIndex, const Assets::UniformBufferObject& ubo);
		void FinalizeTemporalUbo(RenderView& view, Assets::UniformBufferObject& ubo);
		void ComposeViewToSwapchainSubrect(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderView& view);
		void ResolvePrimaryViewToSwapchain(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void CreateStorageImage(uint32_t bindlessIdx, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName);
		void CreateStorageImage(uint32_t bindlessIdx, VkExtent2D extent, VkFormat format, VkImageTiling tiling,
                                VkImageUsageFlags usage, const char* debugName);
		void RequestClearAmbientCubeCache() { ambient_.requestClearCache = true; }
		void RecreateSwapChain();
		void UpdateUniformBuffer(uint32_t imageIndex);

		// Frame stages
		void BeforeNextFrame();
		// Scene-global pre-passes: camera-independent work that runs once per scene frame.
		void BeginSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		// Per-view render: camera/bank-dependent pre-passes, logic renderer, and view-local post.
		void RenderViewToBank(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                              RenderView& view, bool clearSwapchain, LogicRendererBase& logicRenderer);
		void PreRenderPerView(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView);
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void AfterUpdateScene();
		void CaptureFrameGenerationHudless(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		Rendering::Upscaler::FFrameInputs BuildUpscalerFrameInputs(
			VkCommandBuffer commandBuffer,
			uint32_t imageIndex,
			VkImageLayout swapchainLayout);

		// Pre-render passes
		void UpdateSkinningBuffers();
		void UpdateAccelerationStructuresTop(VkCommandBuffer commandBuffer);
		void UpdateAccelerationStructuresBottom(VkCommandBuffer commandBuffer);
		void HandleAmbientCubeCacheInvalidation(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void DispatchSkinning(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void DispatchGpuCulling(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void DispatchClearPass(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool clearSwapchain = true);
		void DispatchVisibilityPass(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void DispatchSunShadow(VkCommandBuffer commandBuffer, uint32_t imageIndex);

		// Post-render passes
		void ClearAmbientCubeCache(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void BakeAmbientCubeCascade(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool useHardware);
		void DispatchVisualDebugger(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void CopyObjectIdHistory(VkCommandBuffer commandBuffer);
		void DrawWireframeOverlay(VkCommandBuffer commandBuffer, uint32_t imageIndex);

		// Acceleration structure lifecycle
		void CreateAccelerationStructures();
		void DeleteAccelerationStructures();
		void CreateBottomLevelStructures(VkCommandBuffer commandBuffer);
		void CreateTopLevelStructures(VkCommandBuffer commandBuffer);
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
		virtual FRendererRequirements Requirements() const { return {}; }
		
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
