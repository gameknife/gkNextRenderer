#include "Engine/Vulkan/GpuQueryTimer.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    bool TryCalculateElapsedMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp,
                                         const float timestampPeriod, const uint32_t timestampValidBits,
                                         float& outMilliseconds)
    {
        outMilliseconds = 0.0f;
        if (timestampPeriod <= 0.0f || timestampValidBits == 0)
        {
            return false;
        }

        const uint32_t validBits = std::min(timestampValidBits, 64u);
        const uint64_t mask = validBits == 64
            ? std::numeric_limits<uint64_t>::max()
            : ((uint64_t{1} << validBits) - 1);
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
    GpuQueryTimer::GpuQueryTimer(const Device& device, const uint32_t totalCount,
                                 const VkPhysicalDeviceProperties& properties)
        : device_(device), timestampPeriod_(properties.limits.timestampPeriod)
    {
        const auto queueFamilies = Vulkan::GetEnumerateVector(device_.PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
        if (device_.GraphicsFamilyIndex() >= queueFamilies.size())
        {
            return;
        }

        timestampValidBits_ = std::min(queueFamilies[device_.GraphicsFamilyIndex()].timestampValidBits, 64u);
        if (timestampPeriod_ == 0.0f || timestampValidBits_ == 0 || totalCount < 2)
        {
            return;
        }

        queryBanks_.resize(queryBankCount);
        for (QueryBank& bank : queryBanks_)
        {
            bank.timestamps.resize(totalCount * 2);
            bank.availability.resize(totalCount);

            VkQueryPoolCreateInfo queryPoolInfo{};
            queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryPoolInfo.queryCount = totalCount;

            const VkResult result = vkCreateQueryPool(device_.Handle(), &queryPoolInfo, nullptr, &bank.queryPool);
            if (result != VK_SUCCESS)
            {
                SPDLOG_WARN("Failed to create timestamp query bank: {}", static_cast<int>(result));
                for (QueryBank& createdBank : queryBanks_)
                {
                    if (createdBank.queryPool != VK_NULL_HANDLE)
                    {
                        vkDestroyQueryPool(device_.Handle(), createdBank.queryPool, nullptr);
                        createdBank.queryPool = VK_NULL_HANDLE;
                    }
                }
                queryBanks_.clear();
                return;
            }
        }

        valid_ = true;
    }

    GpuQueryTimer::~GpuQueryTimer()
    {
        for (QueryBank& bank : queryBanks_)
        {
            if (bank.queryPool != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device_.Handle(), bank.queryPool, nullptr);
            }
        }
    }

    bool GpuQueryTimer::HardwareQueryEnabled() const
    {
        return GOption != nullptr && GOption->HardwareQuery;
    }

    void GpuQueryTimer::BeginFrame(const VkCommandBuffer commandBuffer)
    {
        if (!HardwareQueryEnabled() || !valid_)
        {
            return;
        }

        for (QueryBank& bank : queryBanks_)
        {
            if (bank.hasSubmittedWork && PollBank(bank) && bank.frameSerial > lastResolvedFrameSerial_)
            {
                ResolveBankStats(bank);
                lastResolvedFrameSerial_ = bank.frameSerial;
            }
        }

        const int32_t bankCount = static_cast<int32_t>(queryBanks_.size());
        for (int32_t attempt = 1; attempt <= bankCount; ++attempt)
        {
            const int32_t candidateIndex = (currentBankIndex_ + attempt + bankCount) % bankCount;
            QueryBank& candidate = queryBanks_[candidateIndex];
            if (candidate.hasSubmittedWork && !CanReuseBank(candidate))
            {
                continue;
            }

            vkCmdResetQueryPool(commandBuffer, candidate.queryPool, 0,
                                static_cast<uint32_t>(candidate.timestamps.size() / 2));
            candidate.queryIndex = 0;
            candidate.frameSerial = ++nextFrameSerial_;
            candidate.frameActive = true;
            candidate.hasSubmittedWork = false;
            candidate.scopeTree.Reset();
            candidate.records.clear();
            currentBankIndex_ = candidateIndex;
            return;
        }

        // GPU is more than queryBankCount frames behind.  Do not reset a pool
        // that may still be referenced by an in-flight command buffer.
        currentBankIndex_ = -1;
        static bool warningLogged = false;
        if (!warningLogged)
        {
            warningLogged = true;
            SPDLOG_WARN("GPU timer query banks are all in flight; skipping timestamp scopes for this frame");
        }
    }

    void GpuQueryTimer::EndFrame(const VkCommandBuffer commandBuffer)
    {
        (void)commandBuffer;
        if (!HardwareQueryEnabled() || !valid_ || currentBankIndex_ < 0)
        {
            return;
        }

        QueryBank& bank = queryBanks_[currentBankIndex_];
        bank.frameActive = false;
        bank.hasSubmittedWork = bank.queryIndex > 0;
        if (bank.hasSubmittedWork && PollBank(bank) && bank.frameSerial > lastResolvedFrameSerial_)
        {
            ResolveBankStats(bank);
            lastResolvedFrameSerial_ = bank.frameSerial;
        }
    }

    uint32_t GpuQueryTimer::BeginScope(const VkCommandBuffer commandBuffer, const char* name)
    {
        if (!HardwareQueryEnabled() || !valid_ || currentBankIndex_ < 0)
        {
            return invalidTimerId;
        }

        QueryBank& bank = queryBanks_[currentBankIndex_];
        if (!bank.frameActive || bank.queryIndex + 2 > bank.timestamps.size() / 2)
        {
            return invalidTimerId;
        }

        const uint32_t scopeId = bank.scopeTree.BeginScope(name == nullptr ? "" : name);
        GpuQueryRecord record{};
        record.startQuery = bank.queryIndex++;
        bank.records.push_back(record);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, bank.queryPool, record.startQuery);
        return scopeId;
    }

    void GpuQueryTimer::EndScope(const VkCommandBuffer commandBuffer, const uint32_t scopeId)
    {
        if (!HardwareQueryEnabled() || !valid_ || currentBankIndex_ < 0)
        {
            return;
        }

        QueryBank& bank = queryBanks_[currentBankIndex_];
        if (!bank.frameActive || scopeId == invalidTimerId || scopeId >= bank.records.size())
        {
            return;
        }

        if (bank.queryIndex < bank.timestamps.size() / 2)
        {
            bank.records[scopeId].endQuery = bank.queryIndex++;
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                bank.queryPool, bank.records[scopeId].endQuery);
        }
        bank.scopeTree.EndScope(scopeId);
    }

    void GpuQueryTimer::BeginMarker(const VkCommandBuffer commandBuffer, const char* name)
    {
        device_.DebugUtils().BeginMarker(commandBuffer, name == nullptr ? "" : name);
    }

    void GpuQueryTimer::EndMarker(const VkCommandBuffer commandBuffer)
    {
        device_.DebugUtils().EndMarker(commandBuffer);
    }

    float GpuQueryTimer::GetTime(const char* name) const
    {
        for (const auto& stat : lastStats_)
        {
            if (stat.name == (name == nullptr ? "" : name))
            {
                return stat.milliseconds;
            }
        }
        return 0.0f;
    }

    std::vector<Runtime::ProfileTimerStat> GpuQueryTimer::FetchTimes(const int maxStack) const
    {
        return Runtime::ProfileScopeTree::FilterStats(lastStats_, maxStack);
    }

    bool GpuQueryTimer::PollBank(QueryBank& bank)
    {
        if (!bank.hasSubmittedWork || bank.queryIndex == 0)
        {
            return true;
        }

        const VkResult result = vkGetQueryPoolResults(
            device_.Handle(), bank.queryPool, 0, bank.queryIndex,
            static_cast<size_t>(bank.queryIndex) * sizeof(uint64_t) * 2,
            bank.timestamps.data(), sizeof(uint64_t) * 2,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        if (result != VK_SUCCESS && result != VK_NOT_READY)
        {
            static bool queryWarningLogged = false;
            if (!queryWarningLogged)
            {
                queryWarningLogged = true;
                SPDLOG_WARN("GPU timer query result unavailable: {}", static_cast<int>(result));
            }
            return false;
        }

        bool allReady = result == VK_SUCCESS;
        for (uint32_t queryIndex = 0; queryIndex < bank.queryIndex; ++queryIndex)
        {
            bank.availability[queryIndex] = bank.timestamps[queryIndex * 2 + 1];
            bank.timestamps[queryIndex] = bank.timestamps[queryIndex * 2];
            if (bank.availability[queryIndex] == 0)
            {
                allReady = false;
            }
        }

        for (uint32_t recordIndex = 0; recordIndex < bank.records.size(); ++recordIndex)
        {
            const GpuQueryRecord& record = bank.records[recordIndex];
            if (record.startQuery == invalidTimerId || record.endQuery == invalidTimerId ||
                bank.availability[record.startQuery] == 0 || bank.availability[record.endQuery] == 0)
            {
                continue;
            }

            float elapsedMilliseconds = 0.0f;
            if (TryCalculateElapsedMilliseconds(bank.timestamps[record.startQuery], bank.timestamps[record.endQuery],
                                                timestampPeriod_, timestampValidBits_, elapsedMilliseconds))
            {
                bank.scopeTree.SetElapsedMilliseconds(recordIndex, elapsedMilliseconds);
            }
        }
        return allReady;
    }

    bool GpuQueryTimer::CanReuseBank(QueryBank& bank)
    {
        return !bank.hasSubmittedWork || PollBank(bank);
    }

    void GpuQueryTimer::ResolveBankStats(QueryBank& bank)
    {
        lastStats_ = bank.scopeTree.CollectStats();
    }
}
