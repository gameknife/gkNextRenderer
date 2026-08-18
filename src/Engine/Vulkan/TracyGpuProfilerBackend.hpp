#pragma once

#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
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
    class TracyGpuProfilerBackend final : public Runtime::IGpuProfilerBackend
    {
    public:
        GK_NON_COPIABLE(TracyGpuProfilerBackend)

        TracyGpuProfilerBackend(VkInstance instance, const Device& device, CommandPool& commandPool,
                                bool calibratedTimestampsAvailable);
        ~TracyGpuProfilerBackend() override;

        void BeginFrame(VkCommandBuffer commandBuffer) override;
        void EndFrame(VkCommandBuffer commandBuffer) override;
        uint32_t BeginScope(VkCommandBuffer commandBuffer, const char* name) override;
        void EndScope(VkCommandBuffer commandBuffer, uint32_t scopeId) override;
        float GetTime(const char* name) const override;
        std::vector<Runtime::ProfileTimerStat> FetchTimes(int maxStack) const override;

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
    };
}
