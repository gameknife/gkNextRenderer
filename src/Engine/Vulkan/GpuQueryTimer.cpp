#include "Engine/Vulkan/GpuQueryTimer.hpp"

#include <algorithm>
#include <cmath>

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

    bool TryCalculateElapsedMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp,
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
}

namespace Vulkan
{
    GpuQueryTimer::GpuQueryTimer(const Device& device, uint32_t totalCount, const VkPhysicalDeviceProperties& properties)
        : device_(device)
    {
        timestamps_.resize(totalCount);
        timestampPeriod_ = properties.limits.timestampPeriod;

        const auto queueFamilies = Vulkan::GetEnumerateVector(device_.PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
        if (device_.GraphicsFamilyIndex() >= queueFamilies.size())
        {
            return;
        }

        timestampValidBits_ = std::min(queueFamilies[device_.GraphicsFamilyIndex()].timestampValidBits, 64u);
        if (timestampPeriod_ == 0.0f || timestampValidBits_ == 0)
        {
            return;
        }

        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = static_cast<uint32_t>(timestamps_.size());

        const VkResult result = vkCreateQueryPool(device_.Handle(), &queryPoolInfo, nullptr, &queryPool_);
        if (result != VK_SUCCESS)
        {
            SPDLOG_WARN("Failed to create timestamp query pool: {}", static_cast<int>(result));
            queryPool_ = VK_NULL_HANDLE;
            return;
        }

        valid_ = true;
    }

    GpuQueryTimer::~GpuQueryTimer()
    {
        if (queryPool_ != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_.Handle(), queryPool_, nullptr);
        }
    }

    void GpuQueryTimer::BeginFrame(VkCommandBuffer commandBuffer)
    {
        if (!GOption->HardwareQuery || !valid_)
        {
            return;
        }

        vkCmdResetQueryPool(commandBuffer, queryPool_, 0, static_cast<uint32_t>(timestamps_.size()));
        queryIndex_ = 0;
        frameActive_ = true;

        CalculateStats();
        ResetTimerRecords(records_, activeStack_, rootNameCounts_);
    }

    void GpuQueryTimer::EndFrame(VkCommandBuffer commandBuffer)
    {
        (void)commandBuffer;
        if (!GOption->HardwareQuery || !valid_ || !frameActive_)
        {
            return;
        }

        frameActive_ = false;
        if (queryIndex_ == 0)
        {
            return;
        }

        const VkResult result = vkGetQueryPoolResults(
            device_.Handle(),
            queryPool_,
            0,
            queryIndex_,
            static_cast<size_t>(queryIndex_) * sizeof(uint64_t),
            timestamps_.data(),
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

        for (auto& record : records_)
        {
            if (record.startQuery == invalidTimerId || record.endQuery == invalidTimerId)
            {
                continue;
            }

            float elapsedMilliseconds = 0.0f;
            if (TryCalculateElapsedMilliseconds(timestamps_[record.startQuery],
                                                timestamps_[record.endQuery],
                                                timestampPeriod_,
                                                timestampValidBits_,
                                                elapsedMilliseconds))
            {
                record.elapsedMilliseconds = elapsedMilliseconds;
            }
        }
    }

    uint32_t GpuQueryTimer::BeginScope(VkCommandBuffer commandBuffer, const char* name)
    {
        if (!GOption->HardwareQuery || !valid_)
        {
            return invalidTimerId;
        }
        if (queryIndex_ + activeStack_.size() + 1 >= timestamps_.size())
        {
            return invalidTimerId;
        }

        const char* timerName = name ? name : "";
        const uint32_t scopeId = static_cast<uint32_t>(records_.size());
        GpuQueryRecord record{};
        record.name = timerName;
        record.depth = static_cast<int>(activeStack_.size());
        record.stableKey = BuildStableKey(record.name, activeStack_, records_, rootNameCounts_);
        record.startQuery = queryIndex_;
        records_.push_back(std::move(record));

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool_, queryIndex_);
        device_.DebugUtils().BeginMarker(commandBuffer, timerName);
        queryIndex_++;
        activeStack_.push_back(scopeId);
        return scopeId;
    }

    void GpuQueryTimer::EndScope(VkCommandBuffer commandBuffer, uint32_t scopeId)
    {
        if (scopeId == invalidTimerId || scopeId >= records_.size())
        {
            return;
        }
        if (queryIndex_ >= timestamps_.size())
        {
            device_.DebugUtils().EndMarker(commandBuffer);
            PopActiveTimer(activeStack_, scopeId);
            return;
        }

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_, queryIndex_);
        device_.DebugUtils().EndMarker(commandBuffer);
        records_[scopeId].endQuery = queryIndex_;
        queryIndex_++;
        PopActiveTimer(activeStack_, scopeId);
    }

    float GpuQueryTimer::GetTime(const char* name) const
    {
        for (const auto& stat : lastStats_)
        {
            if (stat.name == name)
            {
                return stat.milliseconds;
            }
        }
        return 0.0f;
    }

    std::vector<Runtime::ProfileTimerStat> GpuQueryTimer::FetchTimes(int maxStack) const
    {
        return FilterTimerStats(lastStats_, maxStack);
    }

    void GpuQueryTimer::CalculateStats()
    {
        lastStats_.clear();
        lastStats_.reserve(records_.size());
        for (const auto& record : records_)
        {
            if (record.elapsedMilliseconds <= 0.0f)
            {
                continue;
            }
            lastStats_.push_back({record.name, record.stableKey, record.elapsedMilliseconds, record.depth});
        }
    }
}
