#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Profiling/ProfileScopeTree.hpp"

#include <limits>
#include <thread>

struct VkCommandBuffer_T;
using VkCommandBuffer = VkCommandBuffer_T*;

namespace Runtime
{
    class IGpuProfilerBackend
    {
    public:
        virtual ~IGpuProfilerBackend() = default;

        virtual void BeginFrame(VkCommandBuffer commandBuffer) = 0;
        virtual void EndFrame(VkCommandBuffer commandBuffer) = 0;
        virtual uint32_t BeginScope(VkCommandBuffer commandBuffer, const char* name) = 0;
        virtual void EndScope(VkCommandBuffer commandBuffer, uint32_t scopeId) = 0;
        virtual void BeginMarker(VkCommandBuffer commandBuffer, const char* name)
        {
            (void)commandBuffer;
            (void)name;
        }
        virtual void EndMarker(VkCommandBuffer commandBuffer)
        {
            (void)commandBuffer;
        }
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
        void BeginGpuMarker(VkCommandBuffer commandBuffer, const char* name);
        void EndGpuMarker(VkCommandBuffer commandBuffer);

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
        ProfileScopeTree cpuScopeTree_;
        std::vector<CpuClock::time_point> cpuStartTimes_;
        std::unique_ptr<IGpuProfilerBackend> gpuProfilerBackend_;
        bool cpuFrameStarted_ = false;
        std::thread::id cpuOwnerThreadId_;
        std::atomic_bool cpuThreadWarningLogged_ = false;

        inline static FrameProfiler* activeProfiler_ = nullptr;
    };

    class ScopedGpuMarker
    {
    public:
        GK_NON_COPIABLE(ScopedGpuMarker)

        ScopedGpuMarker(VkCommandBuffer commandBuffer, FrameProfiler* profiler, const char* name)
            : commandBuffer_(commandBuffer), profiler_(profiler)
        {
            if (profiler_ != nullptr)
            {
                profiler_->BeginGpuMarker(commandBuffer_, name);
            }
        }

        ~ScopedGpuMarker()
        {
            if (profiler_ != nullptr)
            {
                profiler_->EndGpuMarker(commandBuffer_);
            }
        }

    private:
        VkCommandBuffer commandBuffer_;
        FrameProfiler* profiler_;
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

#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
