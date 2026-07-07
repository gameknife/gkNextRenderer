#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Options.hpp"

#include <unordered_map>
#include <vector>

namespace Vulkan
{
    class GpuQueryTimer final : public Runtime::IGpuProfilerBackend
    {
    public:
        GK_NON_COPIABLE(GpuQueryTimer)

        explicit GpuQueryTimer(const Device& device, uint32_t totalCount, const VkPhysicalDeviceProperties& properties);
        ~GpuQueryTimer() override;

        void BeginFrame(VkCommandBuffer commandBuffer) override;
        void EndFrame(VkCommandBuffer commandBuffer) override;
        uint32_t BeginScope(VkCommandBuffer commandBuffer, const char* name) override;
        void EndScope(VkCommandBuffer commandBuffer, uint32_t scopeId) override;
        float GetTime(const char* name) const override;
        std::vector<Runtime::ProfileTimerStat> FetchTimes(int maxStack) const override;

    private:
        static constexpr uint32_t invalidTimerId = Runtime::FrameProfiler::invalidTimerId;

        struct GpuQueryRecord
        {
            std::string name;
            std::string stableKey;
            int depth = 0;
            uint32_t startQuery = invalidTimerId;
            uint32_t endQuery = invalidTimerId;
            float elapsedMilliseconds = 0.0f;
            std::unordered_map<std::string, uint32_t> childNameCounts;
        };

        void CalculateStats();

        std::vector<Runtime::ProfileTimerStat> lastStats_;
        VkQueryPool queryPool_ = VK_NULL_HANDLE;
        std::vector<uint64_t> timestamps_;
        std::vector<GpuQueryRecord> records_;
        std::vector<uint32_t> activeStack_;
        std::unordered_map<std::string, uint32_t> rootNameCounts_;
        const Device& device_;
        uint32_t queryIndex_ = 0;
        float timestampPeriod_ = 1.0f;
        uint32_t timestampValidBits_ = 64;
        bool frameActive_ = false;
        bool valid_ = false;
    };
}
