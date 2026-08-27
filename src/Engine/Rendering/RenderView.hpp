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
#include "Engine/Rendering/PipelineCommon/ResourceStateTracker.hpp"

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

    enum class EHistoryInvalidationReason
    {
        Initial,
        SceneChanged,
        RendererChanged,
        ExtentChanged,
        CameraCut,
        TemporalConfigChanged,
        ViewReused,
        SwapchainRecreated,
    };

    const char* GetHistoryInvalidationReasonName(EHistoryInvalidationReason reason);

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

        // Non-reprojected progressive accumulation counter for this view.
        mutable uint32_t progressiveFrame = 0;

        // Camera jumped / view resized / scene swapped -> drop temporal history next frame.
        mutable bool resetHistory = true;
        mutable uint64_t historyGeneration = 1;
        mutable EHistoryInvalidationReason historyInvalidationReason = EHistoryInvalidationReason::Initial;
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
    // A view's bank base (k * Assets::Bindless::kViewRtBankStride) travels in GPUScene.CustomData0.
    class FBankAllocator
    {
    public:
        // Total banks (including bank 0). Caps concurrent full-history views to bound VRAM.
        // Owned by the bindless registry so the storage array's low region is sized against it.
        static constexpr uint32_t kMaxConcurrentBanks = static_cast<uint32_t>(Assets::Bindless::kMaxViewRtBanks);

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

    struct FRenderViewHandle
    {
        uint32_t bankBase = FBankAllocator::kInvalidBase;
        uint32_t generation = 0;

        bool IsValid() const { return bankBase != FBankAllocator::kInvalidBase && generation != 0; }
        auto operator<=>(const FRenderViewHandle&) const = default;
    };

    // One independent view of a scene. Owns the per-view temporal history and the identity
    // (RT bank base, render extent/offset, output kind, schedule). Vulkan resources are still
    // owned by VulkanBaseRenderer, but their per-view handles are tracked here.
    class RenderView
    {
    public:
        RenderView(uint32_t bankBase, const FViewDesc& desc, std::string debugName, uint32_t generation = 1)
            : bankBase_(bankBase), generation_(generation), desc_(desc), debugName_(std::move(debugName))
        {
        }

        uint32_t              RtBankBase() const { return bankBase_; }
        FRenderViewHandle     Handle() const { return {bankBase_, generation_}; }
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
        PipelineCommon::FResourceStateTracker& ResourceStates() { return resourceStates_; }

        void SetDebugName(std::string debugName) { debugName_ = std::move(debugName); }
        void SetRenderExtent(VkExtent2D extent)
        {
            if (desc_.renderExtent.width != extent.width || desc_.renderExtent.height != extent.height)
            {
                desc_.renderExtent = extent;
                InvalidateTemporalHistory(EHistoryInvalidationReason::ExtentChanged);
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
            // Swapchain recreation replaces every image in this view's RT bank while keeping
            // the same logical bindless IDs. Old states therefore cannot be carried over to
            // the new VkImage handles; their first use must transition from UNDEFINED again.
            resourceStates_.Reset();
        }
        void InvalidateTemporalHistory(
            EHistoryInvalidationReason reason = EHistoryInvalidationReason::CameraCut)
        {
            state_.resetHistory = true;
            state_.progressiveFrame = 0;
            ++state_.historyGeneration;
            state_.historyInvalidationReason = reason;
            prevDepthValid_ = false;
            resourceStates_.Reset();
        }
        void CreateSwapChain(const SwapChain& swapChain)
        {
            (void)swapChain;
        }
        void DeleteSwapChain()
        {
        }

    private:
        uint32_t         bankBase_ = 0;
        uint32_t         generation_ = 0;
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
        PipelineCommon::FResourceStateTracker resourceStates_;
    };

    using FRenderViewPostCallback = std::function<void(RenderView&)>;

    struct FRenderViewScheduleItem
    {
        FRenderViewHandle view;
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
            generations_.fill(0);
            generations_[0] = 1;
            primary_ = std::make_unique<RenderView>(FBankAllocator::PrimaryBankBase(), desc, "primary view", 1);
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

            const uint32_t bankIndex = bankBase / Assets::Bindless::kViewRtBankStride;
            const uint32_t generation = ++generations_[bankIndex];
            additional_.push_back(std::make_unique<RenderView>(bankBase, desc, std::move(debugName), generation));
            return additional_.back().get();
        }

        RenderView* Resolve(const FRenderViewHandle handle) const
        {
            if (!handle.IsValid()) return nullptr;
            if (primary_->Handle() == handle) return primary_.get();
            const auto found = std::find_if(additional_.begin(), additional_.end(),
                [handle](const auto& view) { return view->Handle() == handle; });
            return found == additional_.end() ? nullptr : found->get();
        }

        bool DestroyView(RenderView& view)
        {
            if (view.IsPrimary())
            {
                return false;
            }
            schedule_.erase(std::remove_if(schedule_.begin(), schedule_.end(),
                [&view](const FRenderViewScheduleItem& item) { return item.view == view.Handle(); }), schedule_.end());
            const auto found = std::find_if(additional_.begin(), additional_.end(),
                [&view](const auto& candidate) { return candidate.get() == &view; });
            if (found == additional_.end())
            {
                return false;
            }
            banks_.Release(view.RtBankBase());
            additional_.erase(found);
            return true;
        }

        void ScheduleView(RenderView& view,
                          LogicRendererBase& logicRenderer,
                          bool clearSwapchain,
                          FRenderViewPostCallback postRender = {})
        {
            schedule_.push_back(FRenderViewScheduleItem{
                view.Handle(),
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

        void InvalidateAllTemporalHistory(EHistoryInvalidationReason reason)
        {
            primary_->InvalidateTemporalHistory(reason);
            for (const auto& view : additional_)
            {
                view->InvalidateTemporalHistory(reason);
            }
        }

    private:
        FBankAllocator                          banks_;
        std::array<uint32_t, FBankAllocator::kMaxConcurrentBanks> generations_{};
        std::unique_ptr<RenderView>             primary_;
        std::vector<std::unique_ptr<RenderView>> additional_;
        std::vector<FRenderViewScheduleItem>     schedule_;
    };

    struct FViewRenderContext
    {
        RenderView* view = nullptr;
        Assets::Scene* sceneOverride = nullptr;
        uint32_t bankBase = 0;
        VkExtent2D renderExtent{0, 0};
        VkDeviceAddress cameraAddress = 0;
        FrameBuffer* visibilityFramebuffer = nullptr;
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
        bool DestroyView(RenderView*& view);
        RenderView& EnsureView(
            FRenderViewHandle& handle,
            const FViewDesc& desc,
            std::string debugName,
            bool copyObjectIdHistory);
        bool DestroyView(FRenderViewHandle& handle);
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
        bool CopyRenderOutputToImage(
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
        FViewRenderContext previousContext_{};
    };
}
