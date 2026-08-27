#pragma once

#include "Engine/Runtime/Profiling/TracyIntegration.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <deque>
#include <optional>

#if GK_TRACY_ENABLED
#include <tracy/tracy/TracyVulkan.hpp>
#endif

namespace Vulkan
{
    class TracyGpuProfilerBackend final
    {
    public:
        GK_NON_COPIABLE(TracyGpuProfilerBackend)

        TracyGpuProfilerBackend(VkInstance instance, const Device& device, CommandPool& commandPool,
                                bool calibratedTimestampsAvailable);
        ~TracyGpuProfilerBackend();

        void BeginFrame(VkCommandBuffer commandBuffer);
        GkProfiling::GpuZoneId BeginScope(VkCommandBuffer commandBuffer, const char* name);
        void EndScope(VkCommandBuffer commandBuffer, GkProfiling::GpuZoneId scopeId);

        static TracyGpuProfilerBackend* GetActive();

    private:
        struct ScopeSlot
        {
#if GK_TRACY_ENABLED
            std::optional<tracy::VkCtxScope> zone;
#endif
        };

        const Device& device_;
        CommandPool& commandPool_;
        std::unique_ptr<CommandBuffers> contextCommandBuffers_;
        std::deque<ScopeSlot> scopes_;
#if GK_TRACY_ENABLED
        TracyVkCtx context_ = nullptr;
#endif

        inline static TracyGpuProfilerBackend* active_ = nullptr;
    };
}
