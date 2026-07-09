#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <limits>

struct VkCommandBuffer_T;
using VkCommandBuffer = VkCommandBuffer_T*;

#define GK_CONCAT_IMPL(a, b) a##b
#define GK_CONCAT(a, b) GK_CONCAT_IMPL(a, b)
#define SCOPED_GPU_TIMER_CMD(commandBufferValue, name) Runtime::ScopedGpuProfileScope GK_CONCAT(scopedGpuTimer_, __LINE__)(commandBufferValue, Runtime::FrameProfiler::GetActiveProfiler(), name)
#define SCOPED_GPU_TIMER(name) SCOPED_GPU_TIMER_CMD(commandBuffer, name)
#define SCOPED_CPU_TIMER(name) PERFORMANCEAPI_INSTRUMENT_DATA(name, ""); Runtime::ScopedCpuProfileScope GK_CONCAT(scopedCpuTimer_, __LINE__)(Runtime::FrameProfiler::GetActiveProfiler(), name)

namespace Runtime
{
    struct ProfileTimerStat
    {
        std::string name;
        std::string stableKey;
        float milliseconds = 0.0f;
        int depth = 0;
    };

    class IGpuProfilerBackend
    {
    public:
        virtual ~IGpuProfilerBackend() = default;

        virtual void BeginFrame(VkCommandBuffer commandBuffer) = 0;
        virtual void EndFrame(VkCommandBuffer commandBuffer) = 0;
        virtual uint32_t BeginScope(VkCommandBuffer commandBuffer, const char* name) = 0;
        virtual void EndScope(VkCommandBuffer commandBuffer, uint32_t scopeId) = 0;
        virtual float GetTime(const char* name) const = 0;
        virtual std::vector<ProfileTimerStat> FetchTimes(int maxStack) const = 0;
    };

    class FrameProfiler final
    {
    public:
        GK_NON_COPIABLE(FrameProfiler)

        using CpuClock = std::chrono::high_resolution_clock;
        using TimerStat = ProfileTimerStat;

        static constexpr uint32_t invalidTimerId = std::numeric_limits<uint32_t>::max();

        struct CpuTimerRecord
        {
            std::string name;
            std::string stableKey;
            int depth = 0;
            CpuClock::time_point startTime{};
            float elapsedMilliseconds = 0.0f;
            std::unordered_map<std::string, uint32_t> childNameCounts;
        };

        static FrameProfiler* GetActiveProfiler()
        {
            return activeProfiler_;
        }

        static void SetActiveProfiler(FrameProfiler* profiler)
        {
            activeProfiler_ = profiler;
        }

        FrameProfiler();
        explicit FrameProfiler(std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend);
        ~FrameProfiler();

        void SetGpuProfilerBackend(std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend);

        void BeginGpuFrame(VkCommandBuffer commandBuffer);
        void EndGpuFrame(VkCommandBuffer commandBuffer);
        uint32_t BeginGpuScope(VkCommandBuffer commandBuffer, const char* name);
        void EndGpuScope(VkCommandBuffer commandBuffer, uint32_t scopeId);

        void BeginCpuFrame();
        void EndCpuFrame();
        uint32_t BeginCpuScope(const char* name);
        void EndCpuScope(uint32_t scopeId);

        float GetGpuTime(const char* name) const;
        std::vector<TimerStat> FetchGpuTimes(int maxStack) const;
        std::vector<TimerStat> FetchCpuTimes(int maxStack) const;

    private:
        void CalculateCpuStats();

        std::vector<TimerStat> lastCpuStats_;
        std::vector<CpuTimerRecord> cpuTimerRecords_;
        std::vector<uint32_t> cpuActiveStack_;
        std::unordered_map<std::string, uint32_t> cpuRootNameCounts_;
        std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend_;
        bool cpuFrameStarted_ = false;

        inline static FrameProfiler* activeProfiler_ = nullptr;
    };

    class ScopedGpuProfileScope
    {
    public:
        GK_NON_COPIABLE(ScopedGpuProfileScope)

        ScopedGpuProfileScope(VkCommandBuffer commandBuffer, FrameProfiler* profiler, const char* name)
            : commandBuffer_(commandBuffer), profiler_(profiler)
        {
            if (profiler_ == nullptr)
            {
                return;
            }
            timerId_ = profiler_->BeginGpuScope(commandBuffer_, name);
        }

        ~ScopedGpuProfileScope()
        {
            if (profiler_ != nullptr)
            {
                profiler_->EndGpuScope(commandBuffer_, timerId_);
            }
        }

    private:
        VkCommandBuffer commandBuffer_;
        FrameProfiler* profiler_;
        uint32_t timerId_ = FrameProfiler::invalidTimerId;
    };

    class ScopedCpuProfileScope
    {
    public:
        GK_NON_COPIABLE(ScopedCpuProfileScope)

        ScopedCpuProfileScope(FrameProfiler* profiler, const char* name) : profiler_(profiler)
        {
            if (profiler_ == nullptr)
            {
                return;
            }
            timerId_ = profiler_->BeginCpuScope(name);
        }

        ~ScopedCpuProfileScope()
        {
            if (profiler_ != nullptr)
            {
                profiler_->EndCpuScope(timerId_);
            }
        }

    private:
        FrameProfiler* profiler_;
        uint32_t timerId_ = FrameProfiler::invalidTimerId;
    };
}
