#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Runtime/Profiling/ProfileScopeTree.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Options.hpp"


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
        void BeginMarker(VkCommandBuffer commandBuffer, const char* name) override;
        void EndMarker(VkCommandBuffer commandBuffer) override;
        float GetTime(const char* name) const override;
        std::vector<Runtime::ProfileTimerStat> FetchTimes(int maxStack) const override;

    private:
        static constexpr uint32_t invalidTimerId = Runtime::FrameProfiler::invalidTimerId;
        static constexpr uint32_t queryBankCount = 3;

        struct GpuQueryRecord
        {
            uint32_t startQuery = invalidTimerId;
            uint32_t endQuery = invalidTimerId;
        };

        struct QueryBank
        {
            VkQueryPool queryPool = VK_NULL_HANDLE;
            std::vector<uint64_t> timestamps;
            std::vector<uint64_t> availability;
            Runtime::ProfileScopeTree scopeTree;
            std::vector<GpuQueryRecord> records;
            uint32_t queryIndex = 0;
            uint64_t frameSerial = 0;
            bool frameActive = false;
            bool hasSubmittedWork = false;
        };

        bool HardwareQueryEnabled() const;
        bool PollBank(QueryBank& bank);
        bool CanReuseBank(QueryBank& bank);
        void ResolveBankStats(QueryBank& bank);

        std::vector<Runtime::ProfileTimerStat> lastStats_;
        std::vector<QueryBank> queryBanks_;
        const Device& device_;
        float timestampPeriod_ = 1.0f;
        uint32_t timestampValidBits_ = 64;
        int32_t currentBankIndex_ = -1;
        uint64_t nextFrameSerial_ = 0;
        uint64_t lastResolvedFrameSerial_ = 0;
        bool valid_ = false;
    };
}
