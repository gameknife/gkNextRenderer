#pragma once

#include "Engine/Runtime/Profiling/FrameProfiler.hpp"

namespace Runtime
{
    class FCompositeGpuProfilerBackend final : public IGpuProfilerBackend
    {
    public:
        GK_NON_COPIABLE(FCompositeGpuProfilerBackend)

        FCompositeGpuProfilerBackend() = default;
        ~FCompositeGpuProfilerBackend() override = default;

        void AddBackend(std::unique_ptr<IGpuProfilerBackend> backend);

        void BeginFrame(VkCommandBuffer commandBuffer) override;
        void EndFrame(VkCommandBuffer commandBuffer) override;
        uint32_t BeginScope(VkCommandBuffer commandBuffer, const char* name) override;
        void EndScope(VkCommandBuffer commandBuffer, uint32_t scopeId) override;
        void BeginMarker(VkCommandBuffer commandBuffer, const char* name) override;
        void EndMarker(VkCommandBuffer commandBuffer) override;
        float GetTime(const char* name) const override;
        std::vector<ProfileTimerStat> FetchTimes(int maxStack) const override;

        size_t BackendCount() const { return backends_.size(); }

    private:
        struct ScopeMapping
        {
            std::vector<uint32_t> childScopeIds;
        };

        std::vector<std::unique_ptr<IGpuProfilerBackend>> backends_;
        std::vector<ScopeMapping> scopeMappings_;
    };
}
