#pragma once

#include "StudioSimTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
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
        struct FDecisionLogEntry
        {
            uint64_t id = 0;
            std::string employeeName;
            std::string summary;
            std::string prompt;
            std::string response;
            double elapsedMs = -1.0;
            bool success = false;
        };

        // 主线程每帧调用：排空已完成的决策并 apply，然后视情况发起下一个决策。
        void Tick(double gameMinutes, const FDailyGoal& goal, const FGameProject& gameProject,
                  const std::string& eventsSummary, NextAI::FAIService* ai, std::vector<FEmployee>& employees,
                  const OfficeMap& office);
        void Reset();

        bool InFlight() const { return inFlight_; }
        double InFlightElapsedMs() const;
        int DecisionsMade() const { return decisionsMade_; }
        int FallbacksUsed() const { return fallbacksUsed_; }
        const std::vector<FDecisionLogEntry>& Log() const { return log_; }

    private:
        struct FPendingResult
        {
            size_t          empIndex = 0;
            FDecisionResult result;
            double elapsedMs = 0.0;
            bool serviceSuccess = false;
            std::string errorMessage;
            std::string prompt;
            std::string response;
        };

        void ApplyResult(std::vector<FEmployee>& employees, size_t empIndex, const FDecisionResult& result,
                         double gameMinutes, const OfficeMap& office);
        void ApplyFallback(std::vector<FEmployee>& employees, size_t empIndex, double gameMinutes,
                           const OfficeMap& office, std::string reason, double elapsedMs = -1.0,
                           std::string prompt = {}, std::string response = {});
        void PushLog(const FEmployee& employee, std::string summary, std::string prompt = {},
                     std::string response = {}, double elapsedMs = -1.0, bool success = false);

        std::mutex mutex_;                       // 保护 completed_
        std::vector<FPendingResult> completed_;  // worker push，主线程 drain
        bool inFlight_ = false;                  // 仅主线程读写
        size_t inFlightEmployeeIndex_ = 0;
        std::chrono::steady_clock::time_point inFlightStartedAt_{};
        std::string inFlightPrompt_;
        int decisionsMade_ = 0;
        int fallbacksUsed_ = 0;
        uint64_t generation_ = 0;
        uint64_t nextLogId_ = 1;
        size_t scanCursor_ = 0;
        std::vector<FDecisionLogEntry> log_;
    };
}
