#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Engine/Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/PipelineCommon/ResourceStateTracker.hpp"
#include "Engine/Rendering/PipelineCommon/CheckerboardRendering.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBufferLayout.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include <vector>
#include <memory>
#include <cassert>
#include <functional>
#include <map>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>

namespace Rendering::Upscaler
{
    class IUpscaler;
}

namespace Rendering::Atmosphere
{
    class AtmosphereSubsystem;
}

namespace Vulkan::PipelineCommon
{
    class RestirDI;
}

namespace Vulkan
{
    struct FFrameRenderSettings
    {
        Runtime::Config::UserSettings userSettings{};
        bool progressiveRendering = false;
        bool offlineProgressivePathTracing = false;
        uint32_t progressiveAccumulatedFrames = 0;
        uint32_t progressiveTargetFrames = 1;
    };

    struct FAmbientBakeProgress
    {
        bool active = false;
        uint32_t completedDispatchGroups = 0;
        uint32_t totalDispatchGroups = 0;
    };
    class FActiveRenderViewScope;
    class FrameSubmission;
    class RayTracingSceneBackend;
    class AmbientCubeBaker;
    class GpuDrivenPasses;
    class LightGridBuilder;
    class LogicRendererBase;
    class RenderViewResourceFactory;
    class RenderViewServices;

    enum ERendererType
    {
        ERT_PathTracing,
        ERT_SoftwareTracing,
        ERT_SoftwareModern,
        ERT_VoxelTracing,
        ERT_SoftwareModernNoAmbient,
        ERT_PathTracingLite,
    };

    enum class ESceneResource : uint32_t
    {
        None = 0,
        Voxel = 1u << 0u,
        Ambient = 1u << 1u,
        TLAS = 1u << 2u,
        SHARC = 1u << 3u,
        LightGrid = 1u << 4u,
    };

    enum class EViewPrepass : uint32_t
    {
        None = 0,
        Cull = 1u << 0u,
        Clear = 1u << 1u,
        Visibility = 1u << 2u,
        CSM = 1u << 3u,
    };

    enum class ERenderOutput : uint32_t
    {
        None = 0,
        Color = 1u << 0u,
        Depth = 1u << 1u,
        Motion = 1u << 2u,
        ObjectId = 1u << 3u,
        Normal = 1u << 4u,
        Albedo = 1u << 5u,
        Diffuse = 1u << 6u,
        Specular = 1u << 7u,
    };

    enum class EPostProcess : uint32_t
    {
        None = 0,
        Temporal = 1u << 0u,
        Upscale = 1u << 1u,
        FrameGeneration = 1u << 3u,
        DebugGBuffer = 1u << 4u,
        RayReconstruction = 1u << 5u,
    };

    enum class EHistoryChannel : uint32_t
    {
        None = 0,
        Diffuse = 1u << 0u,
        Specular = 1u << 1u,
        Albedo = 1u << 2u,
        ObjectId = 1u << 3u,
    };

#define GK_RENDER_CONTRACT_FLAGS(EnumType) \
    constexpr EnumType operator|(EnumType lhs, EnumType rhs) \
    { return static_cast<EnumType>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)); } \
    constexpr bool HasAny(EnumType value, EnumType flags) \
    { return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flags)) != 0; } \
    constexpr bool HasAll(EnumType value, EnumType flags) \
    { return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flags)) == static_cast<uint32_t>(flags); }

    GK_RENDER_CONTRACT_FLAGS(ESceneResource)
    GK_RENDER_CONTRACT_FLAGS(EViewPrepass)
    GK_RENDER_CONTRACT_FLAGS(ERenderOutput)
    GK_RENDER_CONTRACT_FLAGS(EPostProcess)
    GK_RENDER_CONTRACT_FLAGS(EHistoryChannel)
#undef GK_RENDER_CONTRACT_FLAGS

    struct FRendererContract
    {
        ESceneResource sceneResources = ESceneResource::None;
        EViewPrepass prepasses = EViewPrepass::None;
        ERenderOutput outputs = ERenderOutput::None;
        EPostProcess post = EPostProcess::None;
        EHistoryChannel history = EHistoryChannel::None;
        bool supportsSceneOverrideWithoutPrepare = false;
        // Shades from the Primary Surface: Core.SurfaceBuild resolves the visibility buffer once
        // into a dense G-buffer and the tile scheduler allocates the shading dispatches from it.
        // Renderers without it decode visibility inline and shade at full rate.
        bool usesPrimarySurface = false;
    };

    struct FRendererRequirements
    {
        bool requestAmbientCube = false;
        bool requestLightGrid = false;
        bool requestRayTracing = false;
        // Needs voxel SDF geometry (matId + per-axis distance field) but not necessarily ambient cubes.
        bool requestVoxelGeometry = false;

        void Merge(const FRendererRequirements& other)
        {
            requestAmbientCube = requestAmbientCube || other.requestAmbientCube;
            requestLightGrid = requestLightGrid || other.requestLightGrid;
            requestRayTracing = requestRayTracing || other.requestRayTracing;
            requestVoxelGeometry = requestVoxelGeometry || other.requestVoxelGeometry;
        }

        // Ambient cube baking implies voxel geometry (the cube bake reads/writes the voxel SDF),
        // so any cube-requesting renderer also needs voxel geometry available.
        bool NeedsVoxelGeometry() const { return requestAmbientCube || requestVoxelGeometry; }
    };

    struct FViewImageUse
    {
        uint32_t bindlessId = 0;
        PipelineCommon::ERenderStage stages = PipelineCommon::ERenderStage::None;
        PipelineCommon::EResourceAccess access = PipelineCommon::EResourceAccess::None;
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
        bool discardPreviousContents = false;
    };

    FRendererRequirements GetRendererRequirements(ERendererType type);
    const FRendererContract& GetRendererContract(ERendererType type);
    const char* GetRendererName(ERendererType type);
    const std::array<ERendererType, 4>& GetReferenceRendererTypes();

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
        static constexpr uint32_t kFramesInFlight = 1;
        VULKAN_NON_COPIABLE(VulkanBaseRenderer)

        VulkanBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers, Instance* instance);
        virtual ~VulkanBaseRenderer();

        // Lifecycle
        void Start();
        void End();
        void DrawFrame();
        void ReloadShaders();
        void RequestRecreateSwapChain() { requestRecreateSwapChain_ = true; }

        // Scene-viewport override. An editor host presents the scene into a sub-rect of the
        // swapchain image (the dockspace central node), so the whole screen-space chain -- render
        // extent, RT bank images, dispatch counts -- must be sized to that rect, not to the window.
        // Without it the shaders read their screen size from full-window RT images and every pass
        // shades the pixels the panels cover. The rect is in framebuffer pixels and persists across
        // swapchain recreation; a size change rebuilds the swapchain resources like a window resize.
        void SetSceneViewportRect(VkRect2D rect);
        void ClearSceneViewportRect();
        bool HasSceneViewportRect() const { return sceneViewport_.requested.has_value(); }
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
        Runtime::FrameProfiler* Profiler() const { return ctx_.frameProfiler.get(); }
        bool HasSwapChain() const { return frame_.swapChain.operator bool(); }
        int FrameCount() const {return frame_.frameCount;}
        uint64_t SceneGeneration() const { return sceneState_.generation; }
        uint64_t RecordingSubmitSerial() const { return frame_.recordingSubmitSerial; }
        uint64_t CompletedSubmitSerial() const { return frame_.completedSubmitSerial; }
        const Assets::UniformBufferObject& LastUniformBufferObject() const { return frame_.lastUBO; }
        const FFrameRenderSettings& FrameSettings() const { return frameSettings_; }
        VkDeviceAddress AtmosphereParamsAddress() const;
        glm::vec3 AtmosphereTransmittanceToSun(float cameraAltitudeKm, float sunZenithCosine) const;
        DeviceMemory* GetScreenShotMemory() const { return screenshot_.bufferMemory.get(); }
        const Buffer* GetScreenShotBuffer() const { return screenshot_.buffer.get(); }
        bool IsScreenShotCaptureReady() const { return screenshot_.captureReady; }
        uint64_t ScreenShotCaptureSubmitSerial() const { return screenshot_.captureSubmitSerial; }

        // Scene
        Assets::Scene& GetScene();
        std::shared_ptr<Assets::Scene> GetSceneShared() const { return sceneState_.scene.lock(); }
        void SetScene(std::shared_ptr<Assets::Scene> scene);
        Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const;
        void OnPreLoadScene();
        void OnPostLoadScene();
        void OnHdrShUpdated();
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
        RenderView& ActiveRenderView() { return activeViewContext_.view != nullptr ? *activeViewContext_.view : PrimaryView(); }
        const RenderView& ActiveRenderView() const { return activeViewContext_.view != nullptr ? *activeViewContext_.view : PrimaryView(); }
        const FViewRenderContext& ActiveViewContext() const { return activeViewContext_; }
        FViewRenderState& PrimaryViewState() { return renderViews_->Primary().State(); }
        const FViewRenderState& PrimaryViewState() const { return renderViews_->Primary().State(); }

        // RT bank base of the view currently being recorded; injected into GPUScene.CustomData0
        // so shaders resolve screen-space RT slots through Bindless::ViewRT. 0 == primary view.
        uint32_t ActiveViewBankBase() const { return activeViewContext_.bankBase; }
        void SetActiveViewBankBase(uint32_t bankBase) { activeViewContext_.bankBase = bankBase; }

        // Camera UBO device address of the view currently being recorded -> GPUScene.Camera.
        // 0 == primary view (uses the per-image uniform buffer).
        VkDeviceAddress ActiveViewCameraAddress(uint32_t imageIndex) const;
        void SetActiveViewCameraAddress(VkDeviceAddress address) { activeViewContext_.cameraAddress = address; }
        VkExtent2D ActiveViewRenderExtent() const;
        void SetActiveViewRenderExtent(VkExtent2D extent) { activeViewContext_.renderExtent = extent; }

        // Renderer registry
        void RegisterLogicRenderer(ERendererType type);
        void SwitchLogicRenderer(ERendererType type);
        ERendererType CurrentLogicRendererType() const { return logicRenderers_.current; }
        FRendererRequirements CurrentRendererRequirements() const;
        FRendererRequirements ActiveRendererRequirements() const;
        FRendererRequirements RegisteredRendererRequirements() const;
        bool ShouldSkipAmbientCubeUpdates() const;
        FAmbientBakeProgress GetAmbientBakeProgress();
        // Drop the baked cube radiance. The scene calls this whenever the probe grid moves under it,
        // because radiance baked for the previous grid would otherwise persist at the new positions.
        void RequestClearAmbientCubeCache() { ambient_.requestClearCache = true; }

        // Capabilities / flags
        bool VisualDebug() const {return visualDebug_;}
        bool SupportsRayTracing() const { return caps_.supportRayTracing; }
        bool SupportsUpscaler(Rendering::Upscaler::EUpscalerType type) const
        {
            return Rendering::Upscaler::SupportsUpscalerType(caps_.supportedUpscalerTypes, type);
        }
        bool SupportsFrameGeneration(Rendering::Upscaler::EUpscalerType type) const
        {
            return Rendering::Upscaler::SupportsUpscalerType(caps_.frameGenerationTypes, type);
        }
        bool SupportReflex() const { return caps_.supportReflex; }
        bool IsTemporalSuperResolutionActive() const { return temporalSuperResolutionActive_; }
        bool IsCheckerboardRenderingActive() const;
        uint32_t CheckerboardDispatchWidth(uint32_t width, const Assets::GPUScene& gpuScene) const;
        void ConfigureCheckerboardShading(Assets::GPUScene& gpuScene, bool allowed = true) const;
        void ResolveCheckerboardShading(
            VkCommandBuffer commandBuffer,
            const Assets::GPUScene& gpuScene,
            PipelineCommon::ECheckerboardResolveSet resolveSet);
        // Does the renderer currently selected shade from the Primary Surface? A property of the
        // renderer (FRendererContract::usesPrimarySurface), not a runtime switch.
        bool IsSurfacePathActive() const;
        // Checkerboard lighting handed to Native TAAU without a reconstruction pass -- the default
        // whenever its preconditions hold. Everything between Core Shading and the upscaler then has
        // to run at the shading rate, which is why this is a whole-frame decision, not a per-pass one.
        bool IsSparseCheckerboardLightingActive() const;
        bool IsFrameGenerationSwapchainRequested() const
        {
            return frameGenerationSwapchainRequested_;
        }
        bool IsRayReconstructionActive() const
        {
            return temporalSuperResolutionActive_ &&
                Rendering::Upscaler::GetUpscalerTypeInfo(
                    static_cast<uint32_t>(activeUpscalerType_)).requiresRayReconstruction;
        }
        bool RequiresInvalidMotionMask() const
        {
            return temporalSuperResolutionActive_ &&
                Rendering::Upscaler::GetUpscalerTypeInfo(
                    static_cast<uint32_t>(activeUpscalerType_)).requiresInvalidMotionMask;
        }
        uint32_t TemporalJitterFrameCount() const;
        uint32_t EffectiveSuperResolutionMode() const { return effectiveSuperResolutionMode_; }
        Rendering::Upscaler::FFrameGenerationState GetFrameGenerationState() const;
        bool HasFullAmbientCubeBudget() const { return caps_.fullAmbientCubeBudget; }
        void SetVisualDebugEnabled(bool enabled) { visualDebug_ = enabled; }
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
            std::function<void()> afterSubmit;
        };
        Delegates& GetDelegates() { return delegates_; }
        const Delegates& GetDelegates() const { return delegates_; }

        // Shared render resources used by logic renderers
        void CaptureScreenShot();
        void RequestScreenShotCapture() { screenshot_.captureRequested = true; }
        bool HasScenePassAfterPrimaryView() const;
        void TransitionSwapchainImage(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                      const PipelineCommon::FImageUse& use, std::string_view passName);
        void ImportSwapchainImageState(uint32_t imageIndex, const PipelineCommon::FImageState& state);
        PipelineCommon::FResourceStateTracker& AuxiliaryImageStates() { return auxiliaryImageStateTracker_; }
        void TransitionActiveViewImages(VkCommandBuffer commandBuffer,
                                        std::initializer_list<FViewImageUse> uses,
                                        std::string_view passName);
        void TransitionViewImages(VkCommandBuffer commandBuffer, RenderView& view,
                                  std::initializer_list<FViewImageUse> uses,
                                  std::string_view passName);
        const RenderImage* GetStorageImage(uint32_t bindlessIdx) const;

        // --- Volume (3D) bindless resources ---
        // Volume views use compact, dimension-specific descriptor arrays rather than the 2D
        // bindless arrays. The shader accessors still take the global RES_VOLUME_BASE slot ids.
        // Volumes are tracked separately from bindless_.images because that vector is wiped on every
        // swapchain recreation, while froxel grids / 3D LUTs are resolution-independent and must
        // survive a resize. Binds the storage view when usage has STORAGE_BIT and the sampled view
        // when it has SAMPLED_BIT; the sampled descriptor declares SHADER_READ_ONLY_OPTIMAL, so the
        // caller must transition the image out of GENERAL before sampling it.
        // Throws if the physical device cannot back the requested 3D format/usage combination.
        void CreateStorageImage3D(uint32_t bindlessIdx, VkExtent3D extent, VkFormat format, VkImageTiling tiling,
                                  VkImageUsageFlags usage, const char* debugName,
                                  const SamplerConfig& samplerConfig = SamplerConfig::VolumeLut());
        void DestroyStorageImage3D(uint32_t bindlessIdx);
        const RenderImage* GetStorageImage3D(uint32_t bindlessIdx) const;

        // Lazily creates the primary progressive history. Once created, it lives until the
        // current swapchain resources are destroyed.
        void EnsureProgressiveRenderTarget();
        // Screen-space RT image of the view currently being recorded (its bank). C++ counterpart
        // of the shader-side Bindless::GetViewStorageTexture; resolves slot through the active
        // view's bank base. Primary view (base 0) == GetStorageImage (legacy absolute).
        const RenderImage* GetViewStorageImage(uint32_t slot) const { return GetStorageImage(ActiveViewBankBase() + slot); }
        std::vector<RayTracing::TopLevelAccelerationStructure>& TLAS();
        VkAccelerationStructureKHR ActiveTLASHandle() const;
        PipelineCommon::RestirDI& RestirDIResources();

        // Narrow scheduling API for render-view providers (thumbnails, offscreen views).
        LogicRendererBase* EnsureLogicRenderer(ERendererType type);
        void ScheduleRenderView(RenderView& view,
                                LogicRendererBase& logicRenderer,
                                bool clearSwapchain,
                                FRenderViewPostCallback postRender = {});
        void SetRenderViewUbo(RenderView& view, uint32_t imageIndex, const Assets::UniformBufferObject& ubo);
        void FinalizeTemporalUbo(RenderView& view, Assets::UniformBufferObject& ubo);
        void ComposeViewToSwapchainSubrect(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderView& view);

    private:
        friend class FActiveRenderViewScope;
        friend class FrameSubmission;
        friend class RayTracingSceneBackend;
        friend class AmbientCubeBaker;
        friend class GpuDrivenPasses;
        friend class LightGridBuilder;
        friend class RenderViewResourceFactory;

        // Internal resource groups
        struct DeviceCaps
        {
            bool supportRayTracing       = false;
            Rendering::Upscaler::FUpscalerTypeMask supportedUpscalerTypes = 0;
            Rendering::Upscaler::FUpscalerTypeMask frameGenerationTypes = 0;
            bool supportReflex           = false;
            bool supportPCL              = false;
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
            uint32_t tlasInstanceCapacity = 0;
        };

        struct AmbientCubePipelines
        {
            static constexpr uint32_t convergencePasses = 32u;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> softBake;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> clearCache;
            bool requestClearCache = true;
            uint64_t dirtyRevision = 0;
            std::array<uint32_t, Assets::CUBE_CASCADE_MAX> nextGroup{};
            std::array<uint32_t, Assets::CUBE_CASCADE_MAX> completedPasses{};
            uint32_t nextCascade = 0;
            uint32_t groupsPerFrame = 1;
            uint32_t lastDispatchedGroups = 0;
            float millisecondsPerGroup = 0.0f;
        };

        struct SkinnedMeshResources
        {
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
            std::unique_ptr<Runtime::FrameProfiler> frameProfiler;
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
            // Swapchain-scoped 2D render targets: every entry is reset by DeleteSwapChain().
            std::vector<std::unique_ptr<RenderImage> > images;
            // Volume resources, keyed by bindless slot. Sparse (slots live in the high registry
            // range) and deliberately outside the swapchain lifecycle.
            std::unordered_map<uint32_t, std::unique_ptr<RenderImage> > volumeImages;
        };

        struct OverlayPipelines
        {
            std::unique_ptr<PipelineCommon::GraphicsPipeline> wireframePipeline;
            std::unique_ptr<PipelineCommon::VisibilityPipeline> visibilityPipeline;
            std::unique_ptr<Shadow::ShadowMapPass> sunShadowPass;
            std::vector<std::unique_ptr<IExternalRenderPass>> externalPasses;
            // Whether a module pass fits the active renderer's contract cannot change between
            // frames, so the skip diagnostic is reported per renderer transition instead of once
            // per pass per frame.
            std::vector<std::string> skipReportedPassNames;
            ERendererType skipReportedRendererType = ERT_PathTracing;
            bool skipReportedRendererValid = false;
            std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> bufferClearPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> temporalPostFilterPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> toneMappingPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> visualDebuggerPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> checkerboardResolvePipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> gpuCullCompactPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> softMeshShaderFinalizePipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> softMeshShaderExpandPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> lightGridBuildPipeline;
            std::unique_ptr<PipelineCommon::ZeroBindPipeline> shadowGpuCullCompactPipeline;
            std::unique_ptr<FrameBuffer> visibilityFrameBuffer;
            std::vector<FrameBuffer> wireframeFrameBuffers;
        };

        struct ShadowCameraFamilyCache
        {
            bool valid = false;
            const Assets::Scene* scene = nullptr;
            uint64_t sceneGeneration = 0;
            uint64_t renderedFrame = std::numeric_limits<uint64_t>::max();
            Assets::CascadeShadowSetup cascades{};
        };

        struct FSceneRenderState
        {
            std::weak_ptr<Assets::Scene> scene;
            uint64_t generation = 0;
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
            std::unique_ptr<Buffer> buffer;
            std::unique_ptr<DeviceMemory> bufferMemory;
            bool captureRequested = false;
            bool captureReady = false;
            bool initialized = false;
            uint64_t captureSubmitSerial = 0;
        };

        struct FrameGenerationResources
        {
            std::vector<std::unique_ptr<RenderImage>> hudlessImages;
        };

        struct TemporalPostFilterResources
        {
            std::unique_ptr<RenderImage> pingImage;
            std::unique_ptr<RenderImage> pongImage;
            bool pingInitialized = false;
            bool pongInitialized = false;
        };

        struct LateToneMappingResources
        {
            std::unique_ptr<RenderImage> inputImage;
            std::unique_ptr<RenderImage> outputImage;
            bool inputInitialized = false;
            bool outputInitialized = false;
        };

        DeviceCaps caps_;
        DeviceContext ctx_;
        FrameResources frame_;
        SkinnedMeshResources skin_;
        BindlessStorageImages bindless_;
        PipelineCommon::FResourceStateTracker visibilityStateTracker_;
        PipelineCommon::FResourceStateTracker swapchainStateTracker_;
        PipelineCommon::FResourceStateTracker auxiliaryImageStateTracker_;
        std::unique_ptr<RayTracingResources> rt_;
        std::unique_ptr<RayTracingSceneBackend> rayTracingSceneBackend_;
        std::unique_ptr<AmbientCubeBaker> ambientCubeBaker_;
        std::unique_ptr<GpuDrivenPasses> gpuDrivenPasses_;
        std::unique_ptr<LightGridBuilder> lightGridBuilder_;
        ScreenshotResources screenshot_;
        FrameGenerationResources frameGeneration_;
        TemporalPostFilterResources temporalPostFilter_;
        LateToneMappingResources lateToneMapping_;
        AmbientCubePipelines ambient_;
        OverlayPipelines overlay_;
        ShadowCameraFamilyCache shadowCameraFamilyCache_;
        LogicRendererRegistry logicRenderers_;
        std::unique_ptr<RenderViewManager> renderViews_ = std::make_unique<RenderViewManager>();
        std::unique_ptr<RenderViewServices> renderViewServices_;
        FViewRenderContext activeViewContext_{};
        Delegates delegates_;
        std::unique_ptr<Rendering::Upscaler::IUpscaler> upscaler_;
        std::unique_ptr<PipelineCommon::RestirDI> restirDI_;
        std::unique_ptr<Rendering::Atmosphere::AtmosphereSubsystem> atmosphere_;

        FSceneRenderState sceneState_;
        FFrameRenderSettings frameSettings_;
        VkPresentModeKHR presentMode_;
        bool forceSDR_{};
        bool tracyCalibratedTimestampsAvailable_ = false;
        bool visualDebug_{};
        bool requestRecreateSwapChain_ = false;
        bool resetUpscalerHistory_ = true;
        Rendering::Upscaler::EUpscalerType activeUpscalerType_ =
            Rendering::Upscaler::EUpscalerType::None;
        bool temporalSuperResolutionActive_ = false;
        // A renderer whose contract lacks depth/motion can never drive a temporal upscaler, so the
        // fallback notice depends only on this pair. Reporting it per swapchain rebuild would repeat
        // it on every window resize.
        Rendering::Upscaler::EUpscalerType upscalerFallbackReportedType_ =
            Rendering::Upscaler::EUpscalerType::None;
        ERendererType upscalerFallbackReportedRenderer_ = ERT_PathTracing;
        bool upscalerFallbackReported_ = false;
        bool frameGenerationSwapchainRequested_ = false;
        uint32_t effectiveSuperResolutionMode_ =
            static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Native);

        struct SceneViewportOverride
        {
            // Set by the host; absent means "render the whole swapchain image".
            std::optional<VkRect2D> requested;
            // Resolved rect seen last frame, used to detect that a drag has settled.
            VkRect2D lastResolved{};
            // Rect the current swapchain resources were built for.
            VkExtent2D built{};
            uint32_t unsettledFrames = 0;
        };
        // A dock splitter drag reports a new size every frame; rebuilding the swapchain on each of
        // them would stall the drag, so wait for the size to hold still (or for the cap below).
        static constexpr uint32_t kSceneViewportResizeDeferFrames = 12;
        SceneViewportOverride sceneViewport_;

        // Device / swapchain internals
        void SelectPhysicalDevice(uint32_t gpuIdx);
        void SetPhysicalDevice(VkPhysicalDevice physicalDevice);
        void SetPhysicalDeviceImpl(
            VkPhysicalDevice physicalDevice,
            std::vector<const char*>& requiredExtensions,
            VkPhysicalDeviceFeatures& deviceFeatures,
            void* nextDeviceFeatures);
        void OnDeviceSet();
        bool IsLogicRendererRegistered(ERendererType type) const;
        ERendererType GetLogicRendererType(const LogicRendererBase& renderer) const;
        void EnsureLogicRendererSwapChain(ERendererType type, LogicRendererBase& logicRenderer);
        void CreateRenderImages();
        void CreateSceneSwapChainResources();
        // Creates the full screen-space RT set at [bankBase + RT_X]. bankBase 0 == primary view.
        void CreateRenderTargetBank(uint32_t bankBase);
        void CreateRenderTargetBank(uint32_t bankBase, VkExtent2D extent);
        bool DispatchScheduledRenderViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ResolvePrimaryViewToSwapchain(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        bool PrepareTemporalPostFilterOutput(
            VkCommandBuffer commandBuffer,
            uint32_t imageIndex,
            Rendering::Upscaler::FFrameInputs& inputs);
        void ApplyTemporalPostFilter(
            VkCommandBuffer commandBuffer,
            uint32_t imageIndex,
            const Rendering::Upscaler::FFrameInputs& inputs);
        void ApplyToneMappingAfterUpscaler(
            VkCommandBuffer commandBuffer,
            uint32_t imageIndex,
            bool sourceIsUpscaled,
            uint32_t sourceViewBankBase,
            VkExtent2D sourceExtent,
            VkExtent2D outputExtent,
            VkOffset2D outputOffset,
            const Assets::UniformBufferObject& outputUbo);
        void CreateStorageImage(uint32_t bindlessIdx, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName);
        void CreateStorageImage(uint32_t bindlessIdx, VkExtent2D extent, VkFormat format, VkImageTiling tiling,
                                VkImageUsageFlags usage, const char* debugName);
        void RecreateSwapChain();
        // Clamps the requested scene viewport against the current swapchain image.
        VkRect2D ResolveSceneViewportRect() const;
        // Publishes the resolved rect for this frame and schedules a rebuild when its size changed.
        void UpdateSceneViewportRect();
        void UpdateUniformBuffer(uint32_t imageIndex);

        // Frame stages
        void BeforeNextFrame();
        // Scene-global pre-passes: camera-independent work that runs once per scene frame.
        void BeginSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        // Per-view render: camera/bank-dependent pre-passes, logic renderer, and view-local post.
        void RenderViewToBank(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                              RenderView& view, bool clearSwapchain, LogicRendererBase& logicRenderer);
        void PreRenderPerView(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
                              const FRendererContract& contract);
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void AfterUpdateScene();
        void CaptureFrameGenerationHudless(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        Rendering::Upscaler::FFrameInputs BuildUpscalerFrameInputs(
            VkCommandBuffer commandBuffer,
            uint32_t imageIndex,
            VkImageLayout swapchainLayout);

        // Pre-render passes
        void UpdateAccelerationStructuresTop(VkCommandBuffer commandBuffer);
        void UpdateAccelerationStructuresBottom(VkCommandBuffer commandBuffer);
        void HandleAmbientCubeCacheInvalidation(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void DispatchSkinning(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void DispatchGpuCulling(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void DispatchLightGridBuild(VkCommandBuffer commandBuffer, uint32_t imageIndex);
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
        
        VulkanBaseRenderer& baseRender_;
        template<typename T>
        T& GetBaseRender() { return static_cast<T&>(baseRender_); }

        const class SwapChain& SwapChain() const { return baseRender_.SwapChain(); }
        class Window& Window() { return baseRender_.Window(); }
        
        const class Device& Device() const { return baseRender_.Device(); }
        class CommandPool& CommandPool() { return baseRender_.CommandPool(); }
        const class DepthBuffer& DepthBuffer() const { return baseRender_.DepthBuffer(); }
        const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return baseRender_.UniformBuffers(); }
        Runtime::FrameProfiler* Profiler() const { return baseRender_.Profiler(); }
        
        const Assets::Scene& GetScene() {return baseRender_.GetScene();}

        int FrameCount() const {return baseRender_.FrameCount();}

        bool VisualDebug() const {return baseRender_.VisualDebug();}
    };

}
