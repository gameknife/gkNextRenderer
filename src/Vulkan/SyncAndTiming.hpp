#pragma once

#include "DebugUtilities.hpp"
#include "Device.hpp"
#include "Options.hpp"
#include <vector>
#include <list>
#include <memory>
#include <unordered_map>
#include <cassert>
#include <chrono>

#define SCOPED_GPU_TIMER_FOLDER(name, folder) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name, folder)
#define SCOPED_GPU_TIMER(name) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name)
#define SCOPED_CPU_TIMER(name) ScopedCpuTimer scopedCpuTimer(GpuTimer(), name)
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
// VulkanGpuTimer
// ============================================================================

class VulkanGpuTimer
{
public:
    DEFAULT_NON_COPIABLE(VulkanGpuTimer)

    VulkanGpuTimer(const Device& device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop) : device_(device)
    {
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
            Check(vkCreateQueryPool(device_.Handle(), &query_pool_info, nullptr, &query_pool_timestamps), "create timestamp pool");
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

        CalculatieGpuStats();
        for (auto& [name, query] : gpu_timer_query_map)
        {
            std::get<1>(gpu_timer_query_map[name]) = 0;
            std::get<0>(gpu_timer_query_map[name]) = 0;
        }
    }

    void CpuFrameEnd()
    {
        // iterate the cpu_timer_query_map
        for (auto& [name, query] : cpu_timer_query_map)
        {
            std::get<2>(cpu_timer_query_map[name]) = std::get<1>(cpu_timer_query_map[name]) - std::get<0>(cpu_timer_query_map[name]);
        }
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
        vkGetQueryPoolResults(
            device_.Handle(),
            query_pool_timestamps,
            0,
            queryIdx,
            time_stamps.size() * sizeof(uint64_t),
            time_stamps.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

        for (auto& [name, query] : gpu_timer_query_map)
        {
            std::get<2>(gpu_timer_query_map[name]) = (time_stamps[std::get<1>(gpu_timer_query_map[name])] - time_stamps[std::get<0>(gpu_timer_query_map[name])]);
        }
    }

    void Start(VkCommandBuffer commandBuffer, const char* name)
    {
        BENCH_MARK_CHECK();
        if (gpu_timer_query_map.find(name) == gpu_timer_query_map.end())
        {
            gpu_timer_query_map[name] = std::make_tuple(0, 0, 0);
        }
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
        device_.DebugUtils().BeginMarker(commandBuffer, name);
        std::get<0>(gpu_timer_query_map[name]) = queryIdx;
        queryIdx++;
    }
    void End(VkCommandBuffer commandBuffer, const char* name)
    {
        BENCH_MARK_CHECK();
        assert(gpu_timer_query_map.find(name) != gpu_timer_query_map.end());
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
        device_.DebugUtils().EndMarker(commandBuffer);
        std::get<1>(gpu_timer_query_map[name]) = queryIdx;
        queryIdx++;
    }
    void StartCpuTimer(const char* name)
    {
        if (cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
        {
            cpu_timer_query_map[name] = std::make_tuple(0, 0, 0);
        }
        std::get<0>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }
    void EndCpuTimer(const char* name)
    {
        assert(cpu_timer_query_map.find(name) != cpu_timer_query_map.end());
        std::get<1>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }
    float GetGpuTime(const char* name)
    {
        if (gpu_timer_query_map.find(name) == gpu_timer_query_map.end())
        {
            return 0;
        }
        return std::get<2>(gpu_timer_query_map[name]) * timeStampPeriod_ * 1e-6f;
    }
    float GetCpuTime(const char* name)
    {
        if (cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
        {
            return 0;
        }
        return std::get<2>(cpu_timer_query_map[name]) * 1e-6f;
    }

    void CalculatieGpuStats()
    {
        lastStats.clear();
        std::list<std::tuple<std::string, float, uint64_t, uint64_t> > order_list;
        for (auto& [name, query] : gpu_timer_query_map)
        {
            if (std::get<2>(gpu_timer_query_map[name]) == 0)
            {
                continue;
            }
            order_list.insert(order_list.begin(), (std::make_tuple(name, GetGpuTime(name.c_str()), std::get<0>(query), std::get<1>(query))));
        }

        order_list.sort([](const std::tuple<std::string, float, uint64_t, uint64_t>& a, const std::tuple<std::string, float, uint64_t, uint64_t>& b) -> bool
        {
            return std::get<2>(a) < std::get<2>(b);
        });

        std::vector<std::tuple<std::string, float, uint64_t, uint64_t>> activeTimers;
        for (auto& [name, time, startIdx, endIdx] : order_list)
        {
            while (!activeTimers.empty() && std::get<3>(activeTimers.back()) < startIdx)
            {
                activeTimers.pop_back();
            }

            int stackDepth = static_cast<int>(activeTimers.size());
            activeTimers.push_back(std::make_tuple(name, time, startIdx, endIdx));
            lastStats.push_back(std::make_tuple(name, stackDepth, time));
        }
    }
    std::vector<std::tuple<std::string, float, int> > FetchAllTimes(int maxStack)
    {
        std::vector<std::tuple<std::string, float, int> > result;
        for (auto& [name, stackDepth, time] : lastStats)
        {
            if (maxStack > stackDepth)
            {
                result.push_back(std::make_tuple(name, time, stackDepth));
            }
        }
        return result;
    }

    // Folder management
    void PushGpuFolder(const std::string& name)
    {
        gpuFolderStack_.push_back(currentGpuFolder_.length());
        currentGpuFolder_ += name;
    }
    void PopGpuFolder()
    {
        if (!gpuFolderStack_.empty())
        {
            currentGpuFolder_.resize(gpuFolderStack_.back());
            gpuFolderStack_.pop_back();
        }
    }
    const std::string& GetCurrentGpuFolder() const { return currentGpuFolder_; }

    void PushCpuFolder(const std::string& name)
    {
        cpuFolderStack_.push_back(currentCpuFolder_.length());
        currentCpuFolder_ += name;
    }
    void PopCpuFolder()
    {
        if (!cpuFolderStack_.empty())
        {
            currentCpuFolder_.resize(cpuFolderStack_.back());
            cpuFolderStack_.pop_back();
        }
    }
    const std::string& GetCurrentCpuFolder() const { return currentCpuFolder_; }

    std::vector<std::tuple<std::string, int, float> > lastStats; // name, depth, duration seconds
    VkQueryPool query_pool_timestamps = VK_NULL_HANDLE;
    std::vector<uint64_t> time_stamps{};
    std::unordered_map<std::string, std::tuple<uint64_t, uint64_t, uint64_t> > gpu_timer_query_map{};
    std::unordered_map<std::string, std::tuple<uint64_t, uint64_t, uint64_t> > cpu_timer_query_map{};
    const Device& device_;
    uint32_t queryIdx = 0;
    float timeStampPeriod_ = 1;
    bool started_ = false;
    bool valid_ = false;

    std::string currentGpuFolder_;
    std::vector<size_t> gpuFolderStack_;
    std::string currentCpuFolder_;
    std::vector<size_t> cpuFolderStack_;
};

// ============================================================================
// ScopedGpuTimer
// ============================================================================

class ScopedGpuTimer
{
public:
    DEFAULT_NON_COPIABLE(ScopedGpuTimer)

    ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name, const char* foldername) : commandBuffer_(commandBuffer), timer_(timer), name_(name)
    {
        timer_->Start(commandBuffer_, name_.c_str());
        folderTimer = true;
        timer_->PushGpuFolder(foldername);
    }
    ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name) : commandBuffer_(commandBuffer), timer_(timer), name_(name)
    {
        timer_->Start(commandBuffer_, (timer_->GetCurrentGpuFolder() + name_).c_str());
    }
    virtual ~ScopedGpuTimer()
    {
        if (folderTimer)
        {
            timer_->PopGpuFolder();
            timer_->End(commandBuffer_, name_.c_str());
        }
        else
        {
            timer_->End(commandBuffer_, (timer_->GetCurrentGpuFolder() + name_).c_str());
        }
    }
    VkCommandBuffer commandBuffer_;
    VulkanGpuTimer* timer_;
    std::string name_;
    bool folderTimer = false;
};

// ============================================================================
// ScopedCpuTimer
// ============================================================================

class ScopedCpuTimer
{
public:
    DEFAULT_NON_COPIABLE(ScopedCpuTimer)

    ScopedCpuTimer(VulkanGpuTimer* timer, const char* name) : timer_(timer), name_(name)
    {
        timer_->StartCpuTimer((timer_->GetCurrentCpuFolder() + name_).c_str());
    }
    virtual ~ScopedCpuTimer()
    {
        timer_->EndCpuTimer((timer_->GetCurrentCpuFolder() + name_).c_str());
    }
    VulkanGpuTimer* timer_;
    std::string name_;

    // Static methods removed as per refactoring plan.
    // If manual folder management is needed, expose methods on VulkanGpuTimer.
};

}
