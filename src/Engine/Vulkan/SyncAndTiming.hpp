#pragma once

#include "DebugUtilities.hpp"
#include "Device.hpp"
#include "Engine/Options.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <limits>

#define GK_CONCAT_IMPL(a, b) a##b
#define GK_CONCAT(a, b) GK_CONCAT_IMPL(a, b)
#define SCOPED_GPU_TIMER_CMD(commandBufferValue, name) ScopedGpuTimer GK_CONCAT(scopedGpuTimer_, __LINE__)(commandBufferValue, VulkanGpuTimer::GetActiveTimer(), name)
#define SCOPED_GPU_TIMER(name) SCOPED_GPU_TIMER_CMD(commandBuffer, name)
#define SCOPED_CPU_TIMER(name) PERFORMANCEAPI_INSTRUMENT_DATA(name, ""); ScopedCpuTimer GK_CONCAT(scopedCpuTimer_, __LINE__)(VulkanGpuTimer::GetActiveTimer(), name)

#define BENCH_MARK_CHECK() if(!GOption->HardwareQuery || !valid_) return

namespace Vulkan
{

// ============================================================================
// Fence
// ============================================================================

class Fence final
{
public:

    Fence(const Fence&) = delete;
    Fence& operator = (const Fence&) = delete;
    Fence& operator = (Fence&&) = delete;

    explicit Fence(const Device& device, bool signaled);
    Fence(Fence&& other) noexcept;
    ~Fence();

    const class Device& Device() const { return device_; }
    const VkFence& Handle() const { return fence_; }

    void Reset();
    void Wait(uint64_t timeout) const;

private:

    const class Device& device_;

    VkFence fence_{};
};

// ============================================================================
// Semaphore
// ============================================================================

class Semaphore final
{
public:

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator = (const Semaphore&) = delete;
    Semaphore& operator = (Semaphore&&) = delete;

    explicit Semaphore(const Device& device);
    Semaphore(Semaphore&& other) noexcept;
    ~Semaphore();

    const class Device& Device() const { return device_; }

private:

    const class Device& device_;

    VULKAN_HANDLE(VkSemaphore, semaphore_)
};

} // namespace Vulkan

// ============================================================================
// VulkanGpuTimer
// ============================================================================

class VulkanGpuTimer
{
public:
    DEFAULT_NON_COPIABLE(VulkanGpuTimer)

    using CpuClock = std::chrono::high_resolution_clock;
    static constexpr uint32_t invalidTimerId = std::numeric_limits<uint32_t>::max();

    struct TimerStat
    {
        std::string name;
        std::string stableKey;
        float milliseconds = 0.0f;
        int depth = 0;
    };

    struct GpuTimerRecord
    {
        std::string name;
        std::string stableKey;
        int depth = 0;
        uint32_t startQuery = invalidTimerId;
        uint32_t endQuery = invalidTimerId;
        float elapsedMilliseconds = 0.0f;
        std::unordered_map<std::string, uint32_t> childNameCounts;
    };

    struct CpuTimerRecord
    {
        std::string name;
        std::string stableKey;
        int depth = 0;
        CpuClock::time_point startTime{};
        CpuClock::time_point endTime{};
        float elapsedMilliseconds = 0.0f;
        std::unordered_map<std::string, uint32_t> childNameCounts;
    };

    static VulkanGpuTimer* GetActiveTimer()
    {
        return activeTimer_;
    }

    static void SetActiveTimer(VulkanGpuTimer* timer)
    {
        activeTimer_ = timer;
    }

    VulkanGpuTimer(const Vulkan::Device& device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop) : device_(device)
    {
        SetActiveTimer(this);
        time_stamps.resize(totalCount);
        timeStampPeriod_ = prop.limits.timestampPeriod;

        if (timeStampPeriod_ == 0)
        {
            valid_ = false;
            return;
        }

        VkQueryPoolCreateInfo query_pool_info{};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_info.queryCount = static_cast<uint32_t>(time_stamps.size());

        // Use try-catch or noexcept check if possible, but here we just rely on Check not crashing the app if we handle it?
        // The project's Check throws runtime_error.
        try
        {
            Vulkan::Check(vkCreateQueryPool(device_.Handle(), &query_pool_info, nullptr, &query_pool_timestamps), "create timestamp pool");
            valid_ = true;
        }
        catch (...)
        {
            valid_ = false;
            query_pool_timestamps = VK_NULL_HANDLE;
        }
    }
    virtual ~VulkanGpuTimer()
    {
        if (GetActiveTimer() == this)
        {
            SetActiveTimer(nullptr);
        }
        if (query_pool_timestamps != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_.Handle(), query_pool_timestamps, nullptr);
        }
    }

    void Reset(VkCommandBuffer commandBuffer)
    {
        BENCH_MARK_CHECK();
        vkCmdResetQueryPool(commandBuffer, query_pool_timestamps, 0, static_cast<uint32_t>(time_stamps.size()));
        queryIdx = 0;
        started_ = true;

        CalculateGpuStats();
        gpuTimerRecords_.clear();
        gpuActiveStack_.clear();
        gpuRootNameCounts_.clear();
    }

    void CpuFrameBegin()
    {
        cpuFrameStarted_ = true;
        cpuTimerRecords_.clear();
        cpuActiveStack_.clear();
        cpuRootNameCounts_.clear();
    }

    void CpuFrameEnd()
    {
        CalculateCpuStats();
        cpuFrameStarted_ = false;
    }

    void FrameEnd(VkCommandBuffer commandBuffer)
    {
        BENCH_MARK_CHECK();
        if (started_)
        {
            started_ = false;
        }
        else
        {
            return;
        }
        if (queryIdx == 0)
        {
            return;
        }
        vkGetQueryPoolResults(
            device_.Handle(),
            query_pool_timestamps,
            0,
            queryIdx,
            time_stamps.size() * sizeof(uint64_t),
            time_stamps.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

        for (auto& record : gpuTimerRecords_)
        {
            if (record.startQuery == invalidTimerId || record.endQuery == invalidTimerId)
            {
                continue;
            }
            record.elapsedMilliseconds =
                (time_stamps[record.endQuery] - time_stamps[record.startQuery]) * timeStampPeriod_ * 1e-6f;
        }
    }

    uint32_t Start(VkCommandBuffer commandBuffer, const char* name)
    {
        if (!GOption->HardwareQuery || !valid_)
        {
            return invalidTimerId;
        }
        if (queryIdx + gpuActiveStack_.size() + 1 >= time_stamps.size())
        {
            return invalidTimerId;
        }

        const uint32_t timerId = static_cast<uint32_t>(gpuTimerRecords_.size());
        GpuTimerRecord record{};
        record.name = name ? name : "";
        record.depth = static_cast<int>(gpuActiveStack_.size());
        record.stableKey = BuildGpuStableKey(record.name);
        record.startQuery = queryIdx;
        gpuTimerRecords_.push_back(std::move(record));

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
        device_.DebugUtils().BeginMarker(commandBuffer, name);
        queryIdx++;
        gpuActiveStack_.push_back(timerId);
        return timerId;
    }

    void End(VkCommandBuffer commandBuffer, uint32_t timerId)
    {
        if (timerId == invalidTimerId || timerId >= gpuTimerRecords_.size())
        {
            return;
        }
        if (queryIdx >= time_stamps.size())
        {
            device_.DebugUtils().EndMarker(commandBuffer);
            if (!gpuActiveStack_.empty() && gpuActiveStack_.back() == timerId)
            {
                gpuActiveStack_.pop_back();
            }
            return;
        }
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
        device_.DebugUtils().EndMarker(commandBuffer);
        gpuTimerRecords_[timerId].endQuery = queryIdx;
        queryIdx++;
        if (!gpuActiveStack_.empty() && gpuActiveStack_.back() == timerId)
        {
            gpuActiveStack_.pop_back();
        }
    }

    uint32_t StartCpuTimer(const char* name)
    {
        if (!cpuFrameStarted_)
        {
            CpuFrameBegin();
        }

        const uint32_t timerId = static_cast<uint32_t>(cpuTimerRecords_.size());
        CpuTimerRecord record{};
        record.name = name ? name : "";
        record.depth = static_cast<int>(cpuActiveStack_.size());
        record.stableKey = BuildCpuStableKey(record.name);
        record.startTime = CpuClock::now();
        cpuTimerRecords_.push_back(std::move(record));
        cpuActiveStack_.push_back(timerId);
        return timerId;
    }

    void EndCpuTimer(uint32_t timerId)
    {
        if (timerId == invalidTimerId || timerId >= cpuTimerRecords_.size())
        {
            return;
        }
        auto& record = cpuTimerRecords_[timerId];
        record.endTime = CpuClock::now();
        record.elapsedMilliseconds = std::chrono::duration<float, std::milli>(record.endTime - record.startTime).count();
        if (!cpuActiveStack_.empty() && cpuActiveStack_.back() == timerId)
        {
            cpuActiveStack_.pop_back();
        }
    }

    float GetGpuTime(const char* name)
    {
        for (const auto& stat : lastStats)
        {
            if (stat.name == name)
            {
                return stat.milliseconds;
            }
        }
        return 0.0f;
    }

    float GetCpuTime(const char* name)
    {
        for (const auto& stat : lastCpuStats)
        {
            if (stat.name == name)
            {
                return stat.milliseconds;
            }
        }
        return 0.0f;
    }

    void CalculateGpuStats()
    {
        lastStats.clear();
        lastStats.reserve(gpuTimerRecords_.size());
        for (const auto& record : gpuTimerRecords_)
        {
            if (record.elapsedMilliseconds <= 0.0f)
            {
                continue;
            }
            lastStats.push_back({record.name, record.stableKey, record.elapsedMilliseconds, record.depth});
        }
    }

    std::vector<TimerStat> FetchAllTimes(int maxStack)
    {
        std::vector<TimerStat> result;
        for (const auto& stat : lastStats)
        {
            if (maxStack > stat.depth)
            {
                result.push_back(stat);
            }
        }
        return result;
    }

    void CalculateCpuStats()
    {
        lastCpuStats.clear();
        lastCpuStats.reserve(cpuTimerRecords_.size());
        for (const auto& record : cpuTimerRecords_)
        {
            if (record.elapsedMilliseconds <= 0.0f)
            {
                continue;
            }
            lastCpuStats.push_back({record.name, record.stableKey, record.elapsedMilliseconds, record.depth});
        }
    }

    std::vector<TimerStat> FetchAllCpuTimes(int maxStack)
    {
        std::vector<TimerStat> result;
        for (const auto& stat : lastCpuStats)
        {
            if (maxStack > stat.depth)
            {
                result.push_back(stat);
            }
        }
        return result;
    }

private:
    std::string BuildGpuStableKey(const std::string& name)
    {
        if (gpuActiveStack_.empty())
        {
            const uint32_t occurrence = gpuRootNameCounts_[name]++;
            return "/" + name + "#" + std::to_string(occurrence);
        }

        auto& parent = gpuTimerRecords_[gpuActiveStack_.back()];
        const uint32_t occurrence = parent.childNameCounts[name]++;
        return parent.stableKey + "/" + name + "#" + std::to_string(occurrence);
    }

    std::string BuildCpuStableKey(const std::string& name)
    {
        if (cpuActiveStack_.empty())
        {
            const uint32_t occurrence = cpuRootNameCounts_[name]++;
            return "/" + name + "#" + std::to_string(occurrence);
        }

        auto& parent = cpuTimerRecords_[cpuActiveStack_.back()];
        const uint32_t occurrence = parent.childNameCounts[name]++;
        return parent.stableKey + "/" + name + "#" + std::to_string(occurrence);
    }

public:
    std::vector<TimerStat> lastStats;
    std::vector<TimerStat> lastCpuStats;
    VkQueryPool query_pool_timestamps = VK_NULL_HANDLE;
    std::vector<uint64_t> time_stamps{};
    std::vector<GpuTimerRecord> gpuTimerRecords_;
    std::vector<CpuTimerRecord> cpuTimerRecords_;
    std::vector<uint32_t> gpuActiveStack_;
    std::vector<uint32_t> cpuActiveStack_;
    std::unordered_map<std::string, uint32_t> gpuRootNameCounts_;
    std::unordered_map<std::string, uint32_t> cpuRootNameCounts_;
    const Vulkan::Device& device_;
    uint32_t queryIdx = 0;
    float timeStampPeriod_ = 1;
    bool started_ = false;
    bool cpuFrameStarted_ = false;
    bool valid_ = false;

private:
    inline static VulkanGpuTimer* activeTimer_ = nullptr;
};

// ============================================================================
// ScopedGpuTimer
// ============================================================================

class ScopedGpuTimer
{
public:
    DEFAULT_NON_COPIABLE(ScopedGpuTimer)

    ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name) : commandBuffer_(commandBuffer), timer_(timer), name_(name ? name : "")
    {
        if (!timer_)
        {
            return;
        }
        timerId_ = timer_->Start(commandBuffer_, name_.c_str());
    }
    virtual ~ScopedGpuTimer()
    {
        if (timer_)
        {
            timer_->End(commandBuffer_, timerId_);
        }
    }
    VkCommandBuffer commandBuffer_;
    VulkanGpuTimer* timer_;
    std::string name_;
    uint32_t timerId_ = VulkanGpuTimer::invalidTimerId;
};

// ============================================================================
// ScopedCpuTimer
// ============================================================================

class ScopedCpuTimer
{
public:
    DEFAULT_NON_COPIABLE(ScopedCpuTimer)

    ScopedCpuTimer(VulkanGpuTimer* timer, const char* name) : timer_(timer), name_(name ? name : "")
    {
        if (!timer_)
        {
            return;
        }
        timerId_ = timer_->StartCpuTimer(name_.c_str());
    }
    virtual ~ScopedCpuTimer()
    {
        if (!timer_)
        {
            return;
        }
        timer_->EndCpuTimer(timerId_);
    }
    VulkanGpuTimer* timer_;
    std::string name_;
    uint32_t timerId_ = VulkanGpuTimer::invalidTimerId;
};
