#include "Engine/Runtime/Profiling/ProfileScopeTree.hpp"

namespace Runtime
{
    void ProfileScopeTree::Reset()
    {
        records_.clear();
        activeStack_.clear();
        rootNameCounts_.clear();
    }

    uint32_t ProfileScopeTree::BeginScope(const std::string_view name)
    {
        const uint32_t scopeId = static_cast<uint32_t>(records_.size());
        Record record{};
        record.name = name;
        record.depth = static_cast<int>(activeStack_.size());
        record.stableKey = BuildStableKey(record.name);
        records_.push_back(std::move(record));
        activeStack_.push_back(scopeId);
        return scopeId;
    }

    void ProfileScopeTree::EndScope(const uint32_t scopeId)
    {
        if (!activeStack_.empty() && activeStack_.back() == scopeId)
        {
            activeStack_.pop_back();
        }
    }

    void ProfileScopeTree::SetElapsedMilliseconds(const uint32_t scopeId, const float milliseconds)
    {
        if (scopeId < records_.size())
        {
            records_[scopeId].elapsedMilliseconds = milliseconds;
        }
    }

    const ProfileScopeTree::Record* ProfileScopeTree::GetRecord(const uint32_t scopeId) const
    {
        return scopeId < records_.size() ? &records_[scopeId] : nullptr;
    }

    std::vector<ProfileTimerStat> ProfileScopeTree::CollectStats() const
    {
        std::vector<ProfileTimerStat> stats;
        stats.reserve(records_.size());
        for (const auto& record : records_)
        {
            if (record.elapsedMilliseconds <= 0.0f)
            {
                continue;
            }
            stats.push_back({record.name, record.stableKey, record.elapsedMilliseconds, record.depth});
        }
        return stats;
    }

    std::vector<ProfileTimerStat> ProfileScopeTree::FilterStats(
        const std::vector<ProfileTimerStat>& stats, const int maxStack)
    {
        std::vector<ProfileTimerStat> result;
        result.reserve(stats.size());
        for (const auto& stat : stats)
        {
            if (maxStack > stat.depth)
            {
                result.push_back(stat);
            }
        }
        return result;
    }

    std::string ProfileScopeTree::BuildStableKey(const std::string& name)
    {
        if (activeStack_.empty())
        {
            const uint32_t occurrence = rootNameCounts_[name]++;
            return "/" + name + "#" + std::to_string(occurrence);
        }

        auto& parent = records_[activeStack_.back()];
        const uint32_t occurrence = parent.childNameCounts[name]++;
        return parent.stableKey + "/" + name + "#" + std::to_string(occurrence);
    }
}
