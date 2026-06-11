#pragma once

#include "AirportSimTypes.h"

#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace NextAI
{
    class FAIService;
}

namespace AirportSim
{
    class AgentSystem;
    class AirportMap;
    class JourneySystem;
    struct FAgent;

    // Layer 1 串行 LLM 决策调度（§5.3，照搬 StudioSim 模式）：同时在途 1 个请求；
    // GenerateTextAsync 回调在 worker 线程只入队，主线程 Tick 消费并 apply。
    // ai == nullptr / 解析失败 / 超时 → 规则 fallback（加权随机 + 预制台词库）。
    class DecisionScheduler
    {
    public:
        void Tick(double gameMinutes, NextAI::FAIService* ai, AgentSystem& agents, AirportMap& map,
                  JourneySystem& journey, bool isNight);
        void Reset();

        bool InFlight() const { return inFlight_; }
        int DecisionsMade() const { return decisionsMade_; }
        int FallbacksUsed() const { return fallbacksUsed_; }
        const std::vector<std::string>& Log() const { return log_; }

    private:
        struct FPendingResult
        {
            int agentId = -1;
            FDecisionResult result;
        };

        void ApplyResult(FAgent& agent, const FDecisionResult& result, double gameMinutes, AgentSystem& agents,
                         AirportMap& map, JourneySystem& journey);
        void ApplyFallback(FAgent& agent, double gameMinutes, AgentSystem& agents, AirportMap& map,
                           JourneySystem& journey, bool isNight);
        void PushLog(std::string line);

        std::mutex mutex_;                      // 保护 completed_
        std::vector<FPendingResult> completed_; // worker push，主线程 drain
        bool inFlight_ = false;                 // 仅主线程读写
        double inFlightSince_ = 0.0;
        int inFlightAgentId_ = -1;
        int decisionsMade_ = 0;
        int fallbacksUsed_ = 0;
        uint64_t generation_ = 0;
        size_t scanCursor_ = 0;                 // 轮询起点：消除"池下标小的人永远先说话"的偏置
        std::mt19937 rng_{99};
        std::vector<std::string> log_;          // 调试面板滚动决策日志
    };
}
