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
// Phase 0/1 foundation: this header defines the per-view temporal state, the RT bank slot
// allocator and the view output/schedule descriptors. Wiring RenderView into the frame loop
// (and giving each view its own RT bank / camera UBO) lands in later phases.

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace Vulkan
{
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
    // (RT bank base, render extent/offset, output kind, schedule). Resource ownership (the
    // private RT bank / depth / camera UBO) and the frame hooks land in later phases; for now
    // the primary view's resources still live in VulkanBaseRenderer's frame_/bindless_.
    class RenderView
    {
    public:
        RenderView(uint32_t bankBase, const FViewDesc& desc)
            : bankBase_(bankBase), desc_(desc)
        {
        }

        uint32_t              RtBankBase() const { return bankBase_; }
        const FViewDesc&      Desc() const { return desc_; }
        VkExtent2D            RenderExtent() const { return desc_.renderExtent; }
        VkOffset2D            RenderOffset() const { return renderOffset_; }
        EViewOutputKind       OutputKind() const { return desc_.outputKind; }
        EViewSchedule         Schedule() const { return desc_.schedule; }
        bool                  IsPrimary() const { return bankBase_ == FBankAllocator::PrimaryBankBase(); }

        FViewRenderState&       State() { return state_; }
        const FViewRenderState& State() const { return state_; }

        void SetRenderExtent(VkExtent2D extent) { desc_.renderExtent = extent; }
        void SetRenderOffset(VkOffset2D offset) { renderOffset_ = offset; }
        void SetSubrect(VkRect2D rect) { desc_.subrect = rect; }

    private:
        uint32_t         bankBase_ = 0;
        FViewDesc        desc_{};
        VkOffset2D       renderOffset_{};
        FViewRenderState state_{};
    };

    // Drives the list of RenderViews for one VulkanBaseRenderer. Currently owns a single
    // primary view (bank 0); CreateView/DestroyView for secondary/offscreen views land in
    // Phase 2.
    class RenderViewManager
    {
    public:
        RenderViewManager()
        {
            FViewDesc desc{};
            desc.outputKind = EViewOutputKind::SwapchainSubrect;
            desc.schedule   = EViewSchedule::Persistent;
            primary_ = std::make_unique<RenderView>(FBankAllocator::PrimaryBankBase(), desc);
        }

        RenderView&       Primary() { return *primary_; }
        const RenderView& Primary() const { return *primary_; }
        FBankAllocator&   Banks() { return banks_; }

    private:
        FBankAllocator                          banks_;
        std::unique_ptr<RenderView>             primary_;
        std::vector<std::unique_ptr<RenderView>> secondary_;
    };
}
