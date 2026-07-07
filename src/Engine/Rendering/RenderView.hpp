#pragma once

// Multi-viewport (RenderView) foundation.
//
// See docs/designs/multi-viewport-renderview-design.md. A RenderView is the unit of
// "one independent view of a scene": its own screen-space RT bank, depth, camera UBO and
// temporal history, rendered through the same logic-renderer code as the primary window.
//
// NOTE: this is NOT the ImGui platform multi-viewport backend
// (src/Application/Editor/Common/MultiViewportBackend.*). That tears docking panels into OS
// windows; this is scene/render multi-viewport. Names here are deliberately RenderView* to
// avoid confusion — do not reuse the MultiViewport* names.
//
// This header defines the per-view temporal/runtime state, the RT bank slot allocator and the
// view output/schedule descriptors. Render resources still live in VulkanBaseRenderer, while
// RenderView carries the per-view handles needed to record a frame.

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/AtrousDenoiser.hpp"
#include "Engine/Rendering/PipelineCommon/TemporalResolve.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Vulkan
{
    class Device;
    class FrameBuffer;
    class LogicRendererBase;
    class RenderImage;
    class Sampler;
    class SwapChain;
    class VulkanBaseRenderer;

    // How a view's composed result is delivered.
    enum class EViewOutputKind
    {
        SwapchainSubrect,  // compose into a sub-rect of the primary swapchain (split-screen)
        OffscreenTexture,  // compose into an offscreen image bound as a bindless sampled texture
    };

    // When a view re-renders.
    enum class EViewSchedule
    {
        Persistent,  // every frame (realtime multi-camera)
        OnDemand,    // only when camera/scene/selection is dirty (static preview)
        Transient,   // render to convergence (or N frames) then recycle the bank (thumbnails)
    };

    // Per-view temporal history. Everything here used to live as a single global instance in
    // NextEngine::FRenderState; with multiple views each needs its own copy so views never share
    // (and thus never pollute) one another's TAA / accumulation / CSM cache.
    //
    // Members stay `mutable` to match the previous FRenderState semantics (it was written from a
    // logically-const camera-UBO assembly path).
    struct FViewRenderState
    {
        // Previous-frame view-projection, for motion vectors / TAA reprojection.
        mutable Assets::UniformBufferObject previousUniformBuffer{};

        // Sun CSM cascade cache + staggered-update bookkeeping.
        mutable Assets::CascadeShadowSetup cachedSunCascades{};
        mutable bool     cachedSunCascadesValid     = false;
        mutable uint32_t sunShadowCascadeUpdateMask = 0;
        mutable uint32_t sunShadowInitializedMask   = 0;
        mutable uint32_t sunShadowDirtyMask         = Assets::Scene::kSunShadowCascadeMask;

        // Progressive accumulation counter for this view (path tracing convergence).
        mutable uint32_t progressiveFrame = 0;

        // Camera jumped / view resized / scene swapped -> drop temporal history next frame.
        mutable bool resetHistory = true;
    };

    // Description used to create a RenderView.
    struct FViewDesc
    {
        VkExtent2D      renderExtent{};
        EViewOutputKind outputKind = EViewOutputKind::OffscreenTexture;
        EViewSchedule   schedule   = EViewSchedule::Persistent;
        VkRect2D        subrect{};          // valid when outputKind == SwapchainSubrect
    };

    // Allocates fixed-stride bindless RT slot banks for views.
    //
    // Bank 0 is reserved for the primary view == the legacy global layout (absolute slots
    // unchanged). Banks 1..kMaxConcurrentBanks-1 are handed out for secondary / offscreen views.
    // A view's bank base (k * Assets::Bindless::kViewRtBankStride) travels in GPUScene.custom_data_0.
    class FBankAllocator
    {
    public:
        // Total banks (including bank 0). Caps concurrent full-history views to bound VRAM.
        static constexpr uint32_t kMaxConcurrentBanks = 8;

        FBankAllocator()
        {
            // Bank 0 is permanently the primary view.
            used_.assign(kMaxConcurrentBanks, false);
            used_[0] = true;
        }

        // Returns a bank base slot, or kInvalidBase if exhausted. Index 0 is never returned here
        // (primary view owns it); callers ask via PrimaryBankBase().
        uint32_t Acquire()
        {
            for (uint32_t k = 1; k < kMaxConcurrentBanks; ++k)
            {
                if (!used_[k])
                {
                    used_[k] = true;
                    return k * Assets::Bindless::kViewRtBankStride;
                }
            }
            return kInvalidBase;
        }

        void Release(uint32_t bankBase)
        {
            if (bankBase == kInvalidBase)
            {
                return;
            }
            const uint32_t k = bankBase / Assets::Bindless::kViewRtBankStride;
            if (k != 0 && k < kMaxConcurrentBanks)
            {
                used_[k] = false;
            }
        }

        static constexpr uint32_t PrimaryBankBase() { return 0; }
        static constexpr uint32_t kInvalidBase = 0xFFFFFFFFu;

    private:
        std::vector<bool> used_;
    };

    // One independent view of a scene. Owns the per-view temporal history and the identity
    // (RT bank base, render extent/offset, output kind, schedule). Vulkan resources are still
    // owned by VulkanBaseRenderer, but their per-view handles are tracked here.
    class RenderView
    {
    public:
        RenderView(uint32_t bankBase, const FViewDesc& desc, std::string debugName)
            : bankBase_(bankBase), desc_(desc), debugName_(std::move(debugName))
        {
        }

        uint32_t              RtBankBase() const { return bankBase_; }
        const char*           DebugName() const { return debugName_.c_str(); }
        const FViewDesc&      Desc() const { return desc_; }
        VkExtent2D            RenderExtent() const { return desc_.renderExtent; }
        VkOffset2D            RenderOffset() const { return renderOffset_; }
        VkExtent2D            AllocatedExtent() const { return allocatedExtent_; }
        EViewOutputKind       OutputKind() const { return desc_.outputKind; }
        EViewSchedule         Schedule() const { return desc_.schedule; }
        bool                  IsPrimary() const { return bankBase_ == FBankAllocator::PrimaryBankBase(); }
        VkDeviceAddress       CameraAddress() const { return cameraAddress_; }
        FrameBuffer*          VisibilityFramebuffer() const { return visibilityFramebuffer_; }
        Assets::Scene*        SceneOverride() const { return sceneOverride_; }
        bool                  CopyObjectIdHistory() const { return copyObjectIdHistory_; }
        bool                  PrevDepthValid() const { return prevDepthValid_; }
        Assets::UniformBuffer* CameraUbo(uint32_t imageIndex = 0)
        {
            if (cameraUboRing_.empty())
            {
                return nullptr;
            }
            return &cameraUboRing_[std::min<uint32_t>(imageIndex, static_cast<uint32_t>(cameraUboRing_.size() - 1))];
        }

        FViewRenderState&       State() { return state_; }
        const FViewRenderState& State() const { return state_; }
        PipelineCommon::AtrousDenoiser& AtrousDenoiser() { return atrousDenoiser_; }
        const PipelineCommon::AtrousDenoiser& AtrousDenoiser() const { return atrousDenoiser_; }
        PipelineCommon::TemporalResolve& TemporalResolve() { return temporalResolve_; }
        const PipelineCommon::TemporalResolve& TemporalResolve() const { return temporalResolve_; }

        void SetDebugName(std::string debugName) { debugName_ = std::move(debugName); }
        void SetRenderExtent(VkExtent2D extent)
        {
            if (desc_.renderExtent.width != extent.width || desc_.renderExtent.height != extent.height)
            {
                desc_.renderExtent = extent;
                InvalidateTemporalHistory();
            }
        }
        void SetRenderOffset(VkOffset2D offset) { renderOffset_ = offset; }
        void SetSubrect(VkRect2D rect) { desc_.subrect = rect; }
        void SetAllocatedExtent(VkExtent2D extent) { allocatedExtent_ = extent; }
        void SetCameraAddress(VkDeviceAddress address) { cameraAddress_ = address; }
        void SetVisibilityFramebuffer(FrameBuffer* framebuffer) { visibilityFramebuffer_ = framebuffer; }
        void SetAllocatedVisibilityFramebuffer(FrameBuffer* framebuffer, VkExtent2D extent)
        {
            visibilityFramebuffer_ = framebuffer;
            allocatedExtent_ = extent;
            prevDepthValid_ = false;
        }
        void SetSceneOverride(Assets::Scene* scene) { sceneOverride_ = scene; }
        void SetCopyObjectIdHistory(bool copyHistory) { copyObjectIdHistory_ = copyHistory; }
        void SetPrevDepthValid(bool valid) { prevDepthValid_ = valid; }
        Assets::UniformBuffer& EnsureCameraUbo(const Device& device, uint32_t imageIndex, uint32_t imageCount)
        {
            imageCount = std::max(1u, imageCount);
            while (cameraUboRing_.size() < imageCount)
            {
                cameraUboRing_.emplace_back(device);
            }
            return cameraUboRing_[std::min<uint32_t>(imageIndex, static_cast<uint32_t>(cameraUboRing_.size() - 1))];
        }
        void ResetCameraUbo()
        {
            cameraUboRing_.clear();
            cameraAddress_ = 0;
        }
        void ResetSwapChainResources()
        {
            allocatedExtent_ = {0, 0};
            visibilityFramebuffer_ = nullptr;
            prevDepthValid_ = false;
            ResetCameraUbo();
        }
        void InvalidateTemporalHistory()
        {
            state_.resetHistory = true;
            prevDepthValid_ = false;
            temporalResolve_.InvalidateHistory();
        }
        void CreateSwapChain(const SwapChain& swapChain)
        {
            atrousDenoiser_.CreateSwapChain(swapChain);
            temporalResolve_.SetupDefaultHistory();
            temporalResolve_.InvalidateHistory();
        }
        void DeleteSwapChain()
        {
            atrousDenoiser_.DeleteSwapChain();
            temporalResolve_.InvalidateHistory();
        }

    private:
        uint32_t         bankBase_ = 0;
        FViewDesc        desc_{};
        std::string      debugName_;
        VkOffset2D       renderOffset_{};
        VkExtent2D       allocatedExtent_{};
        VkDeviceAddress  cameraAddress_ = 0;
        FrameBuffer*     visibilityFramebuffer_ = nullptr;
        Assets::Scene*   sceneOverride_ = nullptr;
        bool             copyObjectIdHistory_ = true;
        bool             prevDepthValid_ = false;
        FViewRenderState state_{};
        std::vector<Assets::UniformBuffer> cameraUboRing_;
        PipelineCommon::AtrousDenoiser atrousDenoiser_;
        PipelineCommon::TemporalResolve temporalResolve_;
    };

    using FRenderViewPostCallback = std::function<void(RenderView&)>;

    struct FRenderViewScheduleItem
    {
        RenderView* view = nullptr;
        LogicRendererBase* logicRenderer = nullptr;
        bool clearSwapchain = false;
        FRenderViewPostCallback postRender;
    };

    // Drives the list of RenderViews for one VulkanBaseRenderer. Bank 0 is the primary view;
    // additional views use fixed-stride banks from FBankAllocator.
    class RenderViewManager
    {
    public:
        RenderViewManager()
        {
            FViewDesc desc{};
            desc.outputKind = EViewOutputKind::SwapchainSubrect;
            desc.schedule   = EViewSchedule::Persistent;
            primary_ = std::make_unique<RenderView>(FBankAllocator::PrimaryBankBase(), desc, "primary view");
        }

        RenderView&       Primary() { return *primary_; }
        const RenderView& Primary() const { return *primary_; }
        FBankAllocator&   Banks() { return banks_; }
        const std::vector<std::unique_ptr<RenderView>>& AdditionalViews() const { return additional_; }
        const std::vector<FRenderViewScheduleItem>& ScheduledViews() const { return schedule_; }

        RenderView* CreateView(const FViewDesc& desc, std::string debugName)
        {
            const uint32_t bankBase = banks_.Acquire();
            if (bankBase == FBankAllocator::kInvalidBase)
            {
                return nullptr;
            }

            additional_.push_back(std::make_unique<RenderView>(bankBase, desc, std::move(debugName)));
            return additional_.back().get();
        }

        void ScheduleView(RenderView& view,
                          LogicRendererBase& logicRenderer,
                          bool clearSwapchain,
                          FRenderViewPostCallback postRender = {})
        {
            schedule_.push_back(FRenderViewScheduleItem{
                &view,
                &logicRenderer,
                clearSwapchain,
                std::move(postRender)});
        }

        void ClearSchedule()
        {
            schedule_.clear();
        }

        void CreateSwapChain(const SwapChain& swapChain)
        {
            primary_->CreateSwapChain(swapChain);
            for (const auto& view : additional_)
            {
                view->CreateSwapChain(swapChain);
            }
        }

        void DeleteSwapChain()
        {
            primary_->DeleteSwapChain();
            for (const auto& view : additional_)
            {
                view->DeleteSwapChain();
            }
        }

        void ResetSwapChainResources()
        {
            primary_->ResetSwapChainResources();
            for (const auto& view : additional_)
            {
                view->ResetSwapChainResources();
            }
        }

        void DestroyAdditionalViews()
        {
            for (const auto& view : additional_)
            {
                banks_.Release(view->RtBankBase());
            }
            additional_.clear();
        }

    private:
        FBankAllocator                          banks_;
        std::unique_ptr<RenderView>             primary_;
        std::vector<std::unique_ptr<RenderView>> additional_;
        std::vector<FRenderViewScheduleItem>     schedule_;
    };

    struct FRenderViewTargetResources
    {
        FRenderViewTargetResources() = default;
        ~FRenderViewTargetResources();

        FRenderViewTargetResources(const FRenderViewTargetResources&) = delete;
        FRenderViewTargetResources& operator=(const FRenderViewTargetResources&) = delete;
        FRenderViewTargetResources(FRenderViewTargetResources&&) noexcept;
        FRenderViewTargetResources& operator=(FRenderViewTargetResources&&) noexcept;

        void ResetSwapChainResources(bool releaseSampledOutput);

        std::unique_ptr<FrameBuffer> visibilityFramebuffer;
        std::unique_ptr<RenderImage> offscreenImage;
        std::unique_ptr<Sampler> offscreenSampler;
        uint32_t outputSampleSlot = std::numeric_limits<uint32_t>::max();
    };

    class RenderViewResourceFactory final
    {
    public:
        explicit RenderViewResourceFactory(VulkanBaseRenderer& renderer);

        RenderView& EnsureView(
            RenderView*& view,
            const FViewDesc& desc,
            std::string debugName,
            bool copyObjectIdHistory);
        std::unique_ptr<FrameBuffer> RebuildVisibilityFramebuffer(RenderView& view, VkExtent2D extent);
        std::unique_ptr<RenderImage> CreateSampledColorImage(VkExtent2D extent, const char* debugName);
        std::unique_ptr<Sampler> CreateClampSampler();
        void BindSampledColorImage(uint32_t sampleSlot, RenderImage& image, Sampler& sampler);
        void EnsureSampledOffscreenTarget(
            RenderView& view,
            FRenderViewTargetResources& target,
            VkExtent2D extent,
            uint32_t sampleSlot,
            const char* debugName);
        bool CopyDenoisedOutputToImage(
            VkCommandBuffer commandBuffer,
            RenderView& view,
            RenderImage& dst,
            VkFilter filter = VK_FILTER_LINEAR);

    private:
        VulkanBaseRenderer& renderer_;
    };

    class FActiveRenderViewScope final
    {
    public:
        FActiveRenderViewScope(VulkanBaseRenderer& renderer, RenderView& view);
        ~FActiveRenderViewScope();

        FActiveRenderViewScope(const FActiveRenderViewScope&) = delete;
        FActiveRenderViewScope& operator=(const FActiveRenderViewScope&) = delete;

    private:
        VulkanBaseRenderer& renderer_;
        uint32_t previousBankBase_ = 0;
        VkExtent2D previousRenderExtent_{0, 0};
        VkDeviceAddress previousCameraAddress_ = 0;
        FrameBuffer* previousVisibilityFrameBuffer_ = nullptr;
        Assets::Scene* previousSceneOverride_ = nullptr;
        RenderView* previousRenderView_ = nullptr;
    };
}
