#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Options.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>

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

// ============================================================================
// TimelineSemaphore
// ============================================================================

class TimelineSemaphore final
{
public:

    TimelineSemaphore(const TimelineSemaphore&) = delete;
    TimelineSemaphore& operator = (const TimelineSemaphore&) = delete;
    TimelineSemaphore& operator = (TimelineSemaphore&&) = delete;

    explicit TimelineSemaphore(const Device& device, uint64_t initialValue = 0);
    TimelineSemaphore(TimelineSemaphore&& other) noexcept;
    ~TimelineSemaphore();

    const class Device& Device() const { return device_; }

    uint64_t CurrentValue() const;
    void Signal(uint64_t value) const;
    void Wait(uint64_t value, uint64_t timeout = UINT64_MAX) const;

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

    VulkanGpuTimer(const Vulkan::Device& device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop);
    virtual ~VulkanGpuTimer();

    void Reset(VkCommandBuffer commandBuffer);
    void CpuFrameBegin();
    void CpuFrameEnd();
    void FrameEnd(VkCommandBuffer commandBuffer);
    uint32_t Start(VkCommandBuffer commandBuffer, const char* name);
    void End(VkCommandBuffer commandBuffer, uint32_t timerId);
    uint32_t StartCpuTimer(const char* name);
    void EndCpuTimer(uint32_t timerId);
    float GetGpuTime(const char* name);
    float GetCpuTime(const char* name);
    void CalculateGpuStats();
    std::vector<TimerStat> FetchAllTimes(int maxStack);
    void CalculateCpuStats();
    std::vector<TimerStat> FetchAllCpuTimes(int maxStack);
    static bool TryCalculateElapsedMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp, float timestampPeriod,
                                                uint32_t timestampValidBits, float& outMilliseconds);

private:
    std::string BuildGpuStableKey(const std::string& name);
    std::string BuildCpuStableKey(const std::string& name);

#if WITH_SUPERLUMINAL
    struct GpuReplayScope
    {
        std::string name;
        uint32_t startQuery = invalidTimerId;
        uint32_t endQuery = invalidTimerId;
        double startNanoseconds = 0.0;
        double endNanoseconds = 0.0;
    };

    using GpuReplayFrame = std::vector<GpuReplayScope>;

    void StartGpuTimerReplayThread();
    void StopGpuTimerReplayThread();
    void SubmitGpuTimerReplayFrame();
    void GpuTimerReplayThreadMain();
    void ReplayGpuTimerFrame(const GpuReplayFrame& frame);
    bool IsGpuReplayStopRequested() const;
#endif

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
    uint32_t timestampValidBits_ = 64;
    bool started_ = false;
    bool cpuFrameStarted_ = false;
    bool valid_ = false;

private:
    inline static VulkanGpuTimer* activeTimer_ = nullptr;

#if WITH_SUPERLUMINAL
    std::thread gpuReplayThread_;
    std::mutex gpuReplayMutex_;
    std::condition_variable gpuReplayCondition_;
    std::deque<GpuReplayFrame> gpuReplayQueue_;
    std::atomic_bool gpuReplayStop_{false};
#endif
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
        timerId_ = timer_->Start(commandBuffer_, name_);
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
    const char* name_;
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
