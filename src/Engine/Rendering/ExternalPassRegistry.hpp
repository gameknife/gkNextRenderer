#pragma once

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vulkan
{
    class VulkanBaseRenderer;

    enum class EExternalPassInsertionPoint
    {
        AfterPrimaryView,
        BeforeSwapchainResolve,
    };

    enum class EExternalPassScope
    {
        PrimaryView,
        EveryView,
        Scene,
    };

    struct FExternalPassContract
    {
        const char* name = "external-pass";
        EExternalPassInsertionPoint insertionPoint = EExternalPassInsertionPoint::AfterPrimaryView;
        EExternalPassScope scope = EExternalPassScope::PrimaryView;
        uint32_t requiredOutputs = 0;
        uint32_t producedOutputs = 0;
        bool supportsSceneOverride = false;
        // Can this pass run when the frame's scene color is sparse (checkerboard lighting handed to
        // Native TAAU without a reconstruction pass)? True for passes that only paint the pixels
        // their own geometry covers - a stray write on an unshaded pixel is at worst a debug-overlay
        // artefact. False for anything that composites over the whole image, because that would turn
        // the unshaded parity into plausible-looking colour the upscaler cannot detect.
        bool supportsSparseShadingRate = false;
    };

    inline bool AreExternalPassInputsAvailable(
        const FExternalPassContract& pass, const uint32_t availableOutputs)
    {
        return (availableOutputs & pass.requiredOutputs) == pass.requiredOutputs;
    }

    // An overlay render pass owned by a module (e.g. DevTools aux drawing) that the
    // renderer creates with the swapchain resources and executes after the primary
    // view each frame. Instances are destroyed on swapchain teardown.
    class IExternalRenderPass
    {
    public:
        virtual ~IExternalRenderPass() = default;
        virtual FExternalPassContract Contract() const = 0;
        virtual void CreateResources() = 0;
        virtual void Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;

        // Will this pass composite over pixels it does not itself rasterize, this frame? A pass
        // that would is incompatible with a sparse scene color, because it turns the parity Core
        // Shading skipped into plausible-looking colour that the temporal upscaler cannot detect.
        // The default answers from the static contract; override when the answer depends on
        // whether the pass actually has content to draw.
        virtual bool PaintsWholeSceneThisFrame() const
        {
            return !Contract().supportsSparseShadingRate;
        }
    };

    using FExternalPassFactory = std::function<std::unique_ptr<IExternalRenderPass>(VulkanBaseRenderer&)>;

    // Register before renderer start (module install time). Lower priority executes
    // first each frame (content passes before debug overlays).
    void RegisterExternalPassFactory(int priority, FExternalPassFactory factory);
    const std::vector<FExternalPassFactory>& ExternalPassFactories();
}
