#include "Engine/Runtime/Profiling/FrameProfiler.hpp"

namespace
{
    template <typename TRecord>
    void ResetTimerRecords(std::vector<TRecord>& records,
                           std::vector<uint32_t>& activeStack,
                           std::unordered_map<std::string, uint32_t>& rootNameCounts)
    {
        records.clear();
        activeStack.clear();
        rootNameCounts.clear();
    }

    void PopActiveTimer(std::vector<uint32_t>& activeStack, uint32_t timerId)
    {
        if (!activeStack.empty() && activeStack.back() == timerId)
        {
            activeStack.pop_back();
        }
    }

    template <typename TRecord>
    std::string BuildStableKey(const std::string& name,
                               const std::vector<uint32_t>& activeStack,
                               std::vector<TRecord>& records,
                               std::unordered_map<std::string, uint32_t>& rootNameCounts)
    {
        if (activeStack.empty())
        {
            const uint32_t occurrence = rootNameCounts[name]++;
            return "/" + name + "#" + std::to_string(occurrence);
        }

        auto& parent = records[activeStack.back()];
        const uint32_t occurrence = parent.childNameCounts[name]++;
        return parent.stableKey + "/" + name + "#" + std::to_string(occurrence);
    }

    template <typename TRecord>
    std::vector<Runtime::ProfileTimerStat> CollectTimerStats(const std::vector<TRecord>& records)
    {
        std::vector<Runtime::ProfileTimerStat> stats;
        stats.reserve(records.size());
        for (const auto& record : records)
        {
            if (record.elapsedMilliseconds <= 0.0f)
            {
                continue;
            }
            stats.push_back({record.name, record.stableKey, record.elapsedMilliseconds, record.depth});
        }
        return stats;
    }

    std::vector<Runtime::ProfileTimerStat> FilterTimerStats(const std::vector<Runtime::ProfileTimerStat>& stats,
                                                            int maxStack)
    {
        std::vector<Runtime::ProfileTimerStat> result;
        for (const auto& stat : stats)
        {
            if (maxStack > stat.depth)
            {
                result.push_back(stat);
            }
        }
        return result;
    }
}

namespace Runtime
{
    FrameProfiler::FrameProfiler()
    {
        SetActiveProfiler(this);
    }

    FrameProfiler::FrameProfiler(std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend)
        : gpuProfilerBackend_(std::move(gpuProfilerBackend))
    {
        SetActiveProfiler(this);
    }

    FrameProfiler::~FrameProfiler()
    {
        if (GetActiveProfiler() == this)
        {
            SetActiveProfiler(nullptr);
        }
    }

    void FrameProfiler::SetGpuProfilerBackend(std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend)
    {
        gpuProfilerBackend_ = std::move(gpuProfilerBackend);
    }

    void FrameProfiler::BeginGpuFrame(VkCommandBuffer commandBuffer)
    {
        if (gpuProfilerBackend_)
        {
            gpuProfilerBackend_->BeginFrame(commandBuffer);
        }
    }

    void FrameProfiler::EndGpuFrame(VkCommandBuffer commandBuffer)
    {
        if (gpuProfilerBackend_)
        {
            gpuProfilerBackend_->EndFrame(commandBuffer);
        }
    }

    uint32_t FrameProfiler::BeginGpuScope(VkCommandBuffer commandBuffer, const char* name)
    {
        return gpuProfilerBackend_ ? gpuProfilerBackend_->BeginScope(commandBuffer, name) : invalidTimerId;
    }

    void FrameProfiler::EndGpuScope(VkCommandBuffer commandBuffer, uint32_t scopeId)
    {
        if (gpuProfilerBackend_)
        {
            gpuProfilerBackend_->EndScope(commandBuffer, scopeId);
        }
    }

    void FrameProfiler::BeginCpuFrame()
    {
        cpuFrameStarted_ = true;
        ResetTimerRecords(cpuTimerRecords_, cpuActiveStack_, cpuRootNameCounts_);
    }

    void FrameProfiler::EndCpuFrame()
    {
        CalculateCpuStats();
        cpuFrameStarted_ = false;
    }

    uint32_t FrameProfiler::BeginCpuScope(const char* name)
    {
        if (!cpuFrameStarted_)
        {
            BeginCpuFrame();
        }

        const uint32_t timerId = static_cast<uint32_t>(cpuTimerRecords_.size());
        CpuTimerRecord record{};
        record.name = name ? name : "";
        record.depth = static_cast<int>(cpuActiveStack_.size());
        record.stableKey = BuildStableKey(record.name, cpuActiveStack_, cpuTimerRecords_, cpuRootNameCounts_);
        record.startTime = CpuClock::now();
        cpuTimerRecords_.push_back(std::move(record));
        cpuActiveStack_.push_back(timerId);
        return timerId;
    }

    void FrameProfiler::EndCpuScope(uint32_t scopeId)
    {
        if (scopeId == invalidTimerId || scopeId >= cpuTimerRecords_.size())
        {
            return;
        }

        auto& record = cpuTimerRecords_[scopeId];
        record.elapsedMilliseconds = std::chrono::duration<float, std::milli>(CpuClock::now() - record.startTime).count();
        PopActiveTimer(cpuActiveStack_, scopeId);
    }

    float FrameProfiler::GetGpuTime(const char* name) const
    {
        return gpuProfilerBackend_ ? gpuProfilerBackend_->GetTime(name) : 0.0f;
    }

    std::vector<FrameProfiler::TimerStat> FrameProfiler::FetchGpuTimes(int maxStack) const
    {
        return gpuProfilerBackend_ ? gpuProfilerBackend_->FetchTimes(maxStack) : std::vector<TimerStat>{};
    }

    std::vector<FrameProfiler::TimerStat> FrameProfiler::FetchCpuTimes(int maxStack) const
    {
        return FilterTimerStats(lastCpuStats_, maxStack);
    }

    void FrameProfiler::CalculateCpuStats()
    {
        lastCpuStats_ = CollectTimerStats(cpuTimerRecords_);
    }
}
