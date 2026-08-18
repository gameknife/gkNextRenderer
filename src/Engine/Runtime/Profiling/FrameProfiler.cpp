#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"

namespace Runtime
{
    FrameProfiler::FrameProfiler()
        : cpuOwnerThreadId_(std::this_thread::get_id())
    {
        SetActiveProfiler(this);
        GkProfiling::SetThreadName("Main");
    }

    FrameProfiler::FrameProfiler(std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend)
        : gpuProfilerBackend_(std::move(gpuProfilerBackend)), cpuOwnerThreadId_(std::this_thread::get_id())
    {
        SetActiveProfiler(this);
        GkProfiling::SetThreadName("Main");
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

    void FrameProfiler::BeginGpuMarker(VkCommandBuffer commandBuffer, const char* name)
    {
        if (gpuProfilerBackend_)
        {
            gpuProfilerBackend_->BeginMarker(commandBuffer, name);
        }
    }

    void FrameProfiler::EndGpuMarker(VkCommandBuffer commandBuffer)
    {
        if (gpuProfilerBackend_)
        {
            gpuProfilerBackend_->EndMarker(commandBuffer);
        }
    }

    void FrameProfiler::BeginCpuFrame()
    {
        if (std::this_thread::get_id() != cpuOwnerThreadId_)
        {
            return;
        }
        cpuFrameStarted_ = true;
        cpuScopeTree_.Reset();
        cpuStartTimes_.clear();
    }

    void FrameProfiler::EndCpuFrame()
    {
        if (std::this_thread::get_id() != cpuOwnerThreadId_)
        {
            return;
        }
        CalculateCpuStats();
        cpuFrameStarted_ = false;
    }

    uint32_t FrameProfiler::BeginCpuScope(const char* name)
    {
        if (std::this_thread::get_id() != cpuOwnerThreadId_)
        {
            bool expected = false;
            if (cpuThreadWarningLogged_.compare_exchange_strong(expected, true))
            {
                SPDLOG_WARN("FrameProfiler CPU aggregation is main-thread only; this scope is Tracy-only");
            }
            return invalidTimerId;
        }

        if (!cpuFrameStarted_)
        {
            BeginCpuFrame();
        }

        const uint32_t timerId = cpuScopeTree_.BeginScope(name ? name : "");
        cpuStartTimes_.push_back(CpuClock::now());
        return timerId;
    }

    void FrameProfiler::EndCpuScope(uint32_t scopeId)
    {
        if (std::this_thread::get_id() != cpuOwnerThreadId_ || scopeId == invalidTimerId)
        {
            return;
        }

        if (scopeId >= cpuStartTimes_.size() || cpuScopeTree_.GetRecord(scopeId) == nullptr)
        {
            return;
        }

        cpuScopeTree_.SetElapsedMilliseconds(
            scopeId,
            std::chrono::duration<float, std::milli>(CpuClock::now() - cpuStartTimes_[scopeId]).count());
        cpuScopeTree_.EndScope(scopeId);
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
        return ProfileScopeTree::FilterStats(lastCpuStats_, maxStack);
    }

    void FrameProfiler::CalculateCpuStats()
    {
        lastCpuStats_ = cpuScopeTree_.CollectStats();
        cpuStartTimes_.clear();
    }
}
