#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <algorithm>
#include <cmath>

#if WITH_SUPERLUMINAL
#include "Superluminal/PerformanceAPI.h"
#endif

namespace Vulkan
{

// ============================================================================
// Fence Implementation
// ============================================================================

Fence::Fence(const class Device& device, const bool signaled) :
    device_(device)
{
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    Check(vkCreateFence(device.Handle(), &fenceInfo, nullptr, &fence_),
        "create fence");
}

Fence::Fence(Fence&& other) noexcept :
    device_(other.device_),
    fence_(other.fence_)
{
    other.fence_ = nullptr;
}

Fence::~Fence()
{
    if (fence_ != nullptr)
    {
        vkDestroyFence(device_.Handle(), fence_, nullptr);
        fence_ = nullptr;
    }
}

void Fence::Reset()
{
    Check(vkResetFences(device_.Handle(), 1, &fence_),
        "reset fence");
}

void Fence::Wait(const uint64_t timeout) const
{
    Check(vkWaitForFences(device_.Handle(), 1, &fence_, VK_TRUE, timeout),
        "wait for fence");
}

// ============================================================================
// Semaphore Implementation
// ============================================================================

Semaphore::Semaphore(const class Device& device) :
    device_(device)
{
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    Check(vkCreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &semaphore_),
        "create semaphores");
}

Semaphore::Semaphore(Semaphore&& other) noexcept :
    device_(other.device_),
    semaphore_(other.semaphore_)
{
    other.semaphore_ = nullptr;
}

Semaphore::~Semaphore()
{
    if (semaphore_ != nullptr)
    {
        vkDestroySemaphore(device_.Handle(), semaphore_, nullptr);
        semaphore_ = nullptr;
    }
}

TimelineSemaphore::TimelineSemaphore(const class Device& device, uint64_t initialValue) :
    device_(device)
{
    VkSemaphoreTypeCreateInfo typeInfo = {};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = initialValue;

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &typeInfo;

    Check(vkCreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &semaphore_),
        "create timeline semaphore");
}

TimelineSemaphore::TimelineSemaphore(TimelineSemaphore&& other) noexcept :
    device_(other.device_),
    semaphore_(other.semaphore_)
{
    other.semaphore_ = nullptr;
}

TimelineSemaphore::~TimelineSemaphore()
{
    if (semaphore_ != nullptr)
    {
        vkDestroySemaphore(device_.Handle(), semaphore_, nullptr);
        semaphore_ = nullptr;
    }
}

uint64_t TimelineSemaphore::CurrentValue() const
{
    uint64_t value = 0;
    Check(vkGetSemaphoreCounterValue(device_.Handle(), semaphore_, &value),
        "get timeline semaphore counter");
    return value;
}

void TimelineSemaphore::Signal(uint64_t value) const
{
    VkSemaphoreSignalInfo signalInfo = {};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signalInfo.semaphore = semaphore_;
    signalInfo.value = value;
    Check(vkSignalSemaphore(device_.Handle(), &signalInfo), "signal timeline semaphore");
}

void TimelineSemaphore::Wait(uint64_t value, uint64_t timeout) const
{
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore_;
    waitInfo.pValues = &value;
    Check(vkWaitSemaphores(device_.Handle(), &waitInfo, timeout), "wait timeline semaphore");
}

}

// ============================================================================
// VulkanGpuTimer Implementation
// ============================================================================

VulkanGpuTimer::VulkanGpuTimer(const Vulkan::Device& device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop) : device_(device)
{
    SetActiveTimer(this);
    time_stamps.resize(totalCount);
    timeStampPeriod_ = prop.limits.timestampPeriod;

    const auto queueFamilies = Vulkan::GetEnumerateVector(device_.PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
    if (device_.GraphicsFamilyIndex() >= queueFamilies.size())
    {
        valid_ = false;
        return;
    }

    timestampValidBits_ = std::min(queueFamilies[device_.GraphicsFamilyIndex()].timestampValidBits, 64u);
    if (timeStampPeriod_ == 0 || timestampValidBits_ == 0)
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
#if WITH_SUPERLUMINAL
        StartGpuTimerReplayThread();
#endif
    }
    catch (...)
    {
        valid_ = false;
        query_pool_timestamps = VK_NULL_HANDLE;
    }
}
VulkanGpuTimer::~VulkanGpuTimer()
{
#if WITH_SUPERLUMINAL
    StopGpuTimerReplayThread();
#endif
    if (GetActiveTimer() == this)
    {
        SetActiveTimer(nullptr);
    }
    if (query_pool_timestamps != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(device_.Handle(), query_pool_timestamps, nullptr);
    }
}

void VulkanGpuTimer::Reset(VkCommandBuffer commandBuffer)
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

void VulkanGpuTimer::CpuFrameBegin()
{
    cpuFrameStarted_ = true;
    cpuTimerRecords_.clear();
    cpuActiveStack_.clear();
    cpuRootNameCounts_.clear();
}

void VulkanGpuTimer::CpuFrameEnd()
{
    CalculateCpuStats();
    cpuFrameStarted_ = false;
}

void VulkanGpuTimer::FrameEnd(VkCommandBuffer commandBuffer)
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
    const VkResult result = vkGetQueryPoolResults(
        device_.Handle(),
        query_pool_timestamps,
        0,
        queryIdx,
        static_cast<size_t>(queryIdx) * sizeof(uint64_t),
        time_stamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS)
    {
        static bool queryWarningLogged = false;
        if (!queryWarningLogged)
        {
            queryWarningLogged = true;
            SPDLOG_WARN("GPU timer query result unavailable: {}", static_cast<int>(result));
        }
        return;
    }

    for (auto& record : gpuTimerRecords_)
    {
        if (record.startQuery == invalidTimerId || record.endQuery == invalidTimerId)
        {
            continue;
        }
        float elapsedMilliseconds = 0.0f;
        if (TryCalculateElapsedMilliseconds(time_stamps[record.startQuery],
                                            time_stamps[record.endQuery],
                                            timeStampPeriod_,
                                            timestampValidBits_,
                                            elapsedMilliseconds))
        {
            record.elapsedMilliseconds = elapsedMilliseconds;
        }
    }
#if WITH_SUPERLUMINAL
    SubmitGpuTimerReplayFrame();
#endif
}

uint32_t VulkanGpuTimer::Start(VkCommandBuffer commandBuffer, const char* name)
{
    if (!GOption->HardwareQuery || !valid_)
    {
        return invalidTimerId;
    }
    if (queryIdx + gpuActiveStack_.size() + 1 >= time_stamps.size())
    {
        return invalidTimerId;
    }

    const char* timerName = name ? name : "";
    const uint32_t timerId = static_cast<uint32_t>(gpuTimerRecords_.size());
    GpuTimerRecord record{};
    record.name = timerName;
    record.depth = static_cast<int>(gpuActiveStack_.size());
    record.stableKey = BuildGpuStableKey(record.name);
    record.startQuery = queryIdx;
    gpuTimerRecords_.push_back(std::move(record));

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
    device_.DebugUtils().BeginMarker(commandBuffer, timerName);
    queryIdx++;
    gpuActiveStack_.push_back(timerId);
    return timerId;
}

void VulkanGpuTimer::End(VkCommandBuffer commandBuffer, uint32_t timerId)
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

uint32_t VulkanGpuTimer::StartCpuTimer(const char* name)
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

void VulkanGpuTimer::EndCpuTimer(uint32_t timerId)
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

float VulkanGpuTimer::GetGpuTime(const char* name)
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

float VulkanGpuTimer::GetCpuTime(const char* name)
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

void VulkanGpuTimer::CalculateGpuStats()
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

std::vector<VulkanGpuTimer::TimerStat> VulkanGpuTimer::FetchAllTimes(int maxStack)
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

void VulkanGpuTimer::CalculateCpuStats()
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

std::vector<VulkanGpuTimer::TimerStat> VulkanGpuTimer::FetchAllCpuTimes(int maxStack)
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

bool VulkanGpuTimer::TryCalculateElapsedMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp,
                                                     float timestampPeriod, uint32_t timestampValidBits,
                                                     float& outMilliseconds)
{
    outMilliseconds = 0.0f;
    if (timestampPeriod <= 0.0f || timestampValidBits == 0)
    {
        return false;
    }

    const uint32_t validBits = std::min(timestampValidBits, 64u);
    const uint64_t mask = validBits == 64 ? std::numeric_limits<uint64_t>::max() : ((uint64_t{1} << validBits) - 1);
    startTimestamp &= mask;
    endTimestamp &= mask;

    uint64_t elapsedTicks = 0;
    if (endTimestamp >= startTimestamp)
    {
        elapsedTicks = endTimestamp - startTimestamp;
    }
    else if (validBits < 64)
    {
        elapsedTicks = (mask - startTimestamp) + endTimestamp + 1;
    }
    else
    {
        return false;
    }

    const double milliseconds = static_cast<double>(elapsedTicks) * static_cast<double>(timestampPeriod) * 1e-6;
    if (!std::isfinite(milliseconds) || milliseconds > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }

    outMilliseconds = static_cast<float>(milliseconds);
    return true;
}

std::string VulkanGpuTimer::BuildGpuStableKey(const std::string& name)
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

std::string VulkanGpuTimer::BuildCpuStableKey(const std::string& name)
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


#if WITH_SUPERLUMINAL
void VulkanGpuTimer::StartGpuTimerReplayThread()
{
    if (gpuReplayThread_.joinable())
    {
        return;
    }

    gpuReplayStop_.store(false, std::memory_order_release);
    gpuReplayThread_ = std::thread([this]()
    {
        GpuTimerReplayThreadMain();
    });
}

void VulkanGpuTimer::StopGpuTimerReplayThread()
{
    gpuReplayStop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(gpuReplayMutex_);
        gpuReplayQueue_.clear();
    }
    gpuReplayCondition_.notify_all();

    if (gpuReplayThread_.joinable())
    {
        gpuReplayThread_.join();
    }
}

bool VulkanGpuTimer::IsGpuReplayStopRequested() const
{
    return gpuReplayStop_.load(std::memory_order_acquire);
}

void VulkanGpuTimer::SubmitGpuTimerReplayFrame()
{
    if (!valid_ || gpuTimerRecords_.empty() || time_stamps.empty())
    {
        return;
    }

    uint32_t frameStartQuery = invalidTimerId;
    for (const auto& record : gpuTimerRecords_)
    {
        if (record.startQuery == invalidTimerId || record.endQuery == invalidTimerId)
        {
            continue;
        }
        if (record.endQuery >= time_stamps.size() || record.startQuery >= time_stamps.size())
        {
            continue;
        }
        if (time_stamps[record.endQuery] <= time_stamps[record.startQuery])
        {
            continue;
        }

        frameStartQuery = std::min(frameStartQuery, record.startQuery);
    }

    if (frameStartQuery == invalidTimerId)
    {
        return;
    }

    const uint64_t frameStartTimestamp = time_stamps[frameStartQuery];
    GpuReplayFrame replayFrame;
    replayFrame.reserve(gpuTimerRecords_.size());

    for (const auto& record : gpuTimerRecords_)
    {
        if (record.startQuery == invalidTimerId || record.endQuery == invalidTimerId)
        {
            continue;
        }
        if (record.endQuery >= time_stamps.size() || record.startQuery >= time_stamps.size())
        {
            continue;
        }
        if (time_stamps[record.endQuery] <= time_stamps[record.startQuery])
        {
            continue;
        }

        GpuReplayScope scope{};
        scope.name = record.name;
        scope.startQuery = record.startQuery;
        scope.endQuery = record.endQuery;
        scope.startNanoseconds = static_cast<double>(time_stamps[record.startQuery] - frameStartTimestamp) *
            static_cast<double>(timeStampPeriod_);
        scope.endNanoseconds = static_cast<double>(time_stamps[record.endQuery] - frameStartTimestamp) *
            static_cast<double>(timeStampPeriod_);
        replayFrame.push_back(std::move(scope));
    }

    if (replayFrame.empty())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(gpuReplayMutex_);
        while (gpuReplayQueue_.size() >= 2)
        {
            gpuReplayQueue_.pop_front();
        }
        gpuReplayQueue_.push_back(std::move(replayFrame));
    }
    gpuReplayCondition_.notify_one();
}

void VulkanGpuTimer::GpuTimerReplayThreadMain()
{
    PerformanceAPI::SetCurrentThreadName("GPU(Emulated)");
    for (;;)
    {
        GpuReplayFrame frame;
        {
            std::unique_lock<std::mutex> lock(gpuReplayMutex_);
            gpuReplayCondition_.wait(lock, [this]()
            {
                return IsGpuReplayStopRequested() || !gpuReplayQueue_.empty();
            });

            if (IsGpuReplayStopRequested() && gpuReplayQueue_.empty())
            {
                return;
            }

            frame = std::move(gpuReplayQueue_.front());
            gpuReplayQueue_.pop_front();
        }

        ReplayGpuTimerFrame(frame);
    }
}

void VulkanGpuTimer::ReplayGpuTimerFrame(const GpuReplayFrame& frame)
{
    struct ReplayEvent
    {
        uint32_t query = invalidTimerId;
        uint32_t scope = invalidTimerId;
        bool begin = false;
    };

    std::vector<ReplayEvent> events;
    events.reserve(frame.size() * 2);
    for (uint32_t index = 0; index < frame.size(); ++index)
    {
        events.push_back({frame[index].startQuery, index, true});
        events.push_back({frame[index].endQuery, index, false});
    }

    std::sort(events.begin(), events.end(), [](const ReplayEvent& lhs, const ReplayEvent& rhs)
    {
        return lhs.query < rhs.query;
    });

    using ReplayClock = CpuClock;
    const auto replayStartTime = ReplayClock::now();
    uint32_t openScopeCount = 0;

    for (const auto& event : events)
    {
        const auto& scope = frame[event.scope];
        const double targetNanoseconds = event.begin ? scope.startNanoseconds : scope.endNanoseconds;
        const auto targetTime = replayStartTime + std::chrono::duration_cast<ReplayClock::duration>(
            std::chrono::duration<double, std::nano>(targetNanoseconds));

        while (!IsGpuReplayStopRequested() && ReplayClock::now() < targetTime)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        if (IsGpuReplayStopRequested())
        {
            break;
        }

        if (event.begin)
        {
            PerformanceAPI::BeginEvent("[gpu]", scope.name.c_str());
            ++openScopeCount;
        }
        else if (openScopeCount > 0)
        {
            PerformanceAPI::EndEvent();
            --openScopeCount;
        }
    }

    while (openScopeCount > 0)
    {
        PerformanceAPI::EndEvent();
        --openScopeCount;
    }
}
#endif
