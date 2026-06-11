#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <algorithm>

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
