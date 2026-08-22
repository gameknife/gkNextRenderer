#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <array>
#include <span>
#include <vector>

namespace Rendering
{
    struct FRendererChoice
    {
        const char* stableId;
        const char* displayName;
        Vulkan::ERendererType type;
        bool requiresRayTracing;
        bool requiresFullAmbientCubeBudget;
    };

    struct FRendererChoiceCapabilities
    {
        bool supportsRayTracing = false;
        bool hasFullAmbientCubeBudget = false;
        // Whether the device can back the full bindless descriptor arrays. When it cannot, none of
        // the catalog entries can create their resources and ERT_Compatibility is the only answer.
        bool hasFullBindlessBudget = true;
    };

    enum class ERendererFallbackReason : uint8_t
    {
        None,
        UnknownRenderer,
        RayTracingUnavailable,
        AmbientCubeBudgetUnavailable,
        BindlessBudgetUnavailable,
    };

    struct FResolvedRendererChoice
    {
        Vulkan::ERendererType type = Vulkan::ERT_SoftwareModernNoAmbient;
        ERendererFallbackReason reason = ERendererFallbackReason::None;
    };

    std::span<const FRendererChoice> RendererChoiceCatalog();
    const FRendererChoice* FindRendererChoice(Vulkan::ERendererType type);
    bool IsRendererChoiceAvailable(Vulkan::ERendererType type, FRendererChoiceCapabilities capabilities);
    std::vector<const FRendererChoice*> AvailableRendererChoices(FRendererChoiceCapabilities capabilities);
    Vulkan::ERendererType ResolveRendererChoice(Vulkan::ERendererType requested,
                                                 FRendererChoiceCapabilities capabilities);
    FResolvedRendererChoice ResolveRendererChoiceDetailed(Vulkan::ERendererType requested,
                                                           FRendererChoiceCapabilities capabilities);
}
