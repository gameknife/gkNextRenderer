#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <string_view>
#include <limits>

namespace Runtime
{
    struct ProfileTimerStat
    {
        std::string name;
        std::string stableKey;
        float milliseconds = 0.0f;
        int depth = 0;
    };

    // Shared call-tree bookkeeping for CPU and GPU profilers.  The tree owns
    // stable keys and the active stack; timer-specific data (timestamps, CPU
    // clocks, etc.) stays with the backend using it.
    class ProfileScopeTree final
    {
    public:
        static constexpr uint32_t invalidTimerId = std::numeric_limits<uint32_t>::max();

        struct Record
        {
            std::string name;
            std::string stableKey;
            int depth = 0;
            float elapsedMilliseconds = 0.0f;
            std::unordered_map<std::string, uint32_t> childNameCounts;
        };

        void Reset();
        uint32_t BeginScope(std::string_view name);
        void EndScope(uint32_t scopeId);
        void SetElapsedMilliseconds(uint32_t scopeId, float milliseconds);

        const Record* GetRecord(uint32_t scopeId) const;
        size_t Size() const { return records_.size(); }

        std::vector<ProfileTimerStat> CollectStats() const;
        static std::vector<ProfileTimerStat> FilterStats(const std::vector<ProfileTimerStat>& stats, int maxStack);

    private:
        std::string BuildStableKey(const std::string& name);

        std::vector<Record> records_;
        std::vector<uint32_t> activeStack_;
        std::unordered_map<std::string, uint32_t> rootNameCounts_;
    };
}
