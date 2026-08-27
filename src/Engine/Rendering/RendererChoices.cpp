#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Rendering/RendererChoices.hpp"

// Runtime renderer-selection policy follows the installed renderer implementations.

namespace Rendering
{
    namespace
    {
        constexpr std::array<FRendererChoice, 6> rendererChoices{{
            {"software-tracing", "SoftwareTracing", Vulkan::ERT_SoftwareTracing, false, true},
            {"software-modern", "SoftwareModern", Vulkan::ERT_SoftwareModern, false, true},
            {"software-modern-no-ambient", "SoftwareModernNoAmbient", Vulkan::ERT_SoftwareModernNoAmbient, false, false},
            {"voxel-tracing", "VoxelTracing", Vulkan::ERT_VoxelTracing, false, true},
            {"path-tracing", "PathTracing", Vulkan::ERT_PathTracing, true, true},
            {"path-tracing-lite", "PathTracingLite", Vulkan::ERT_PathTracingLite, true, true},
        }};
    }

    std::span<const FRendererChoice> RendererChoiceCatalog()
    {
        return rendererChoices;
    }

    const FRendererChoice* FindRendererChoice(const Vulkan::ERendererType type)
    {
        const auto found = std::find_if(rendererChoices.begin(), rendererChoices.end(),
                                        [type](const FRendererChoice& choice) { return choice.type == type; });
        return found != rendererChoices.end() ? &*found : nullptr;
    }

    bool IsRendererChoiceAvailable(const Vulkan::ERendererType type, const FRendererChoiceCapabilities capabilities)
    {
        const FRendererChoice* choice = FindRendererChoice(type);
        // Every catalog entry builds the full bindless resource set, so the bindless budget gates
        // all of them rather than being a per-choice requirement.
        return choice != nullptr && capabilities.hasFullBindlessBudget &&
            (!choice->requiresRayTracing || capabilities.supportsRayTracing) &&
            (!choice->requiresFullAmbientCubeBudget || capabilities.hasFullAmbientCubeBudget);
    }

    std::vector<const FRendererChoice*> AvailableRendererChoices(const FRendererChoiceCapabilities capabilities)
    {
        std::vector<const FRendererChoice*> result;
        for (const FRendererChoice& choice : rendererChoices)
        {
            if (IsRendererChoiceAvailable(choice.type, capabilities))
            {
                result.push_back(&choice);
            }
        }
        return result;
    }

    Vulkan::ERendererType ResolveRendererChoice(const Vulkan::ERendererType requested,
                                                 const FRendererChoiceCapabilities capabilities)
    {
        return ResolveRendererChoiceDetailed(requested, capabilities).type;
    }

    FResolvedRendererChoice ResolveRendererChoiceDetailed(
        const Vulkan::ERendererType requested, const FRendererChoiceCapabilities capabilities)
    {
        // A device that cannot back the bindless arrays has exactly one option, and it outranks
        // whatever the config asked for -- no catalog entry could create its resources there.
        // Conversely, a capable device never stays on Compatibility: it is not in the catalog, so
        // it falls through to the normal resolution below.
        if (!capabilities.hasFullBindlessBudget)
        {
            return {Vulkan::ERT_Compatibility,
                    requested == Vulkan::ERT_Compatibility
                        ? ERendererFallbackReason::None
                        : ERendererFallbackReason::BindlessBudgetUnavailable};
        }

        if (IsRendererChoiceAvailable(requested, capabilities))
        {
            return {requested, ERendererFallbackReason::None};
        }

        ERendererFallbackReason reason = ERendererFallbackReason::UnknownRenderer;
        if (const FRendererChoice* choice = FindRendererChoice(requested))
        {
            reason = choice->requiresRayTracing && !capabilities.supportsRayTracing
                ? ERendererFallbackReason::RayTracingUnavailable
                : ERendererFallbackReason::AmbientCubeBudgetUnavailable;
        }

        if (IsRendererChoiceAvailable(Vulkan::ERT_SoftwareTracing, capabilities))
        {
            return {Vulkan::ERT_SoftwareTracing, reason};
        }
        return {Vulkan::ERT_SoftwareModernNoAmbient, reason};
    }
}
