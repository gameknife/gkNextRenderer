#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"

#include <algorithm>

namespace Vulkan
{
    IRenderViewProvider* RenderViewServices::RegisterProvider(
        const std::string_view name,
        const int priority,
        std::unique_ptr<IRenderViewProvider> provider)
    {
        IRenderViewProvider* raw = provider.get();
        providers_.push_back({std::string(name), priority, std::move(provider)});
        std::stable_sort(providers_.begin(), providers_.end(),
                         [](const FEntry& a, const FEntry& b) { return a.priority < b.priority; });
        return raw;
    }

    IRenderViewProvider* RenderViewServices::FindProvider(const std::string_view name) const
    {
        for (const FEntry& entry : providers_)
        {
            if (entry.name == name)
            {
                return entry.provider.get();
            }
        }
        return nullptr;
    }

    void RenderViewServices::BeforeNextFrame()
    {
        for (const FEntry& entry : providers_)
        {
            entry.provider->BeforeNextFrame();
        }
    }

    void RenderViewServices::OnMainSceneChanged()
    {
        for (const FEntry& entry : providers_)
        {
            entry.provider->OnMainSceneChanged();
        }
    }

    void RenderViewServices::OnHdrShUpdated()
    {
        for (const FEntry& entry : providers_)
        {
            entry.provider->OnHdrShUpdated();
        }
    }

    void RenderViewServices::OnSwapChainResourcesInvalidated(const bool releaseOffscreenSampledOutputs)
    {
        for (const FEntry& entry : providers_)
        {
            entry.provider->OnSwapChainResourcesInvalidated(releaseOffscreenSampledOutputs);
        }
    }

    bool RenderViewServices::HasWork() const
    {
        for (const FEntry& entry : providers_)
        {
            if (entry.provider->HasWork())
            {
                return true;
            }
        }
        return false;
    }

    void RenderViewServices::ScheduleViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        for (const FEntry& entry : providers_)
        {
            if (entry.provider->ScheduleViews(commandBuffer, imageIndex))
            {
                return;
            }
        }
    }

    bool RenderViewServices::ScheduleReferenceViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        bool renderedAny = false;
        for (const FEntry& entry : providers_)
        {
            renderedAny = entry.provider->ScheduleReferenceViews(commandBuffer, imageIndex) || renderedAny;
        }
        return renderedAny;
    }

    void RenderViewServices::ClearOffscreenFrameRequests()
    {
        for (const FEntry& entry : providers_)
        {
            entry.provider->ClearFrameRequests();
        }
    }
}
