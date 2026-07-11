#pragma once

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <set>
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
        virtual void ReloadShaders(const std::set<std::string>& changedShaderFiles,
                                   std::set<std::string>& handledShaderFiles) {}
    };

    using FExternalPassFactory = std::function<std::unique_ptr<IExternalRenderPass>(VulkanBaseRenderer&)>;

    // Register before renderer start (module install time). Lower priority executes
    // first each frame (content passes before debug overlays).
    void RegisterExternalPassFactory(int priority, FExternalPassFactory factory);
    const std::vector<FExternalPassFactory>& ExternalPassFactories();
}
