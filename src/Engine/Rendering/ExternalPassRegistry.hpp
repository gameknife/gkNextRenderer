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

    // An overlay render pass owned by a module (e.g. DevTools aux drawing) that the
    // renderer creates with the swapchain resources and executes after the primary
    // view each frame. Instances are destroyed on swapchain teardown.
    class IExternalRenderPass
    {
    public:
        virtual ~IExternalRenderPass() = default;
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
