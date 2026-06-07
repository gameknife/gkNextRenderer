#pragma once

#include "StudioSimTypes.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace NextAI
{
    class FAIService;
}

namespace StudioSim
{
    struct FEmployee;
    class OfficeMap;

    // 串行 LLM 决策调度器（见计划 §9）。匹配本地 llama-server 的单实例串行
    // （parallel:1）：同一时刻最多 1 个在途请求。`GenerateTextAsync` 的回调在
    // worker 线程触发，只把解析结果入队；所有员工状态/Scene 改动在主线程 Tick 完成。
    class DecisionScheduler
    {
    public:
        // 主线程每帧调用：排空已完成的决策并 apply，然后视情况发起下一个决策。
        void Tick(double gameMinutes, const FDailyGoal& goal, const std::string& eventsSummary,
                  NextAI::FAIService* ai, std::vector<FEmployee>& employees, const OfficeMap& office);
        void Reset();

        bool InFlight() const { return inFlight_; }
        int DecisionsMade() const { return decisionsMade_; }

    private:
        struct FPendingResult
        {
            size_t          empIndex = 0;
            FDecisionResult result;
        };

        void ApplyResult(std::vector<FEmployee>& employees, size_t empIndex, const FDecisionResult& result,
                         double gameMinutes, const OfficeMap& office);

        std::mutex mutex_;                       // 保护 completed_
        std::vector<FPendingResult> completed_;  // worker push，主线程 drain
        bool inFlight_ = false;                  // 仅主线程读写
        int decisionsMade_ = 0;
        uint64_t generation_ = 0;
    };
}
