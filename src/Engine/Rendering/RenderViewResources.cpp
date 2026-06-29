#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/RenderViewResources.hpp"

#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

namespace Vulkan
{
    FRenderViewTargetResources::~FRenderViewTargetResources() = default;
    FRenderViewTargetResources::FRenderViewTargetResources(FRenderViewTargetResources&&) noexcept = default;
    FRenderViewTargetResources& FRenderViewTargetResources::operator=(FRenderViewTargetResources&&) noexcept = default;

    void FRenderViewTargetResources::ResetSwapChainResources(const bool releaseSampledOutput)
    {
        visibilityFramebuffer.reset();
        if (releaseSampledOutput)
        {
            offscreenImage.reset();
            offscreenSampler.reset();
            outputSampleSlot = std::numeric_limits<uint32_t>::max();
        }
    }
}
