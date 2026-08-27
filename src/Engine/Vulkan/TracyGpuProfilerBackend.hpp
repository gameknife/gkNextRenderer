#pragma once

#include "Engine/Runtime/Profiling/TracyIntegration.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <deque>
#include <optional>

#if GK_TRACY_ENABLED
// GCC 16 的 -Wmaybe-uninitialized 对 tracy VkCtx::Collect 里经由函数指针写出的 tgpu 报误报；
// Tracy 是第三方代码不可改，用 pragma 把抑制范围限制在本头文件引入的翻译单元。
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <tracy/tracy/TracyVulkan.hpp>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
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
