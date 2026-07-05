#pragma once

#include "AirportSimTypes.h"

#include <chrono>
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
        struct FDecisionLogEntry
        {
            uint64_t id = 0;
            int agentId = -1;
            std::string agentName;
            std::string timeLabel;
            std::string summary;
            std::string prompt;
            std::string response;
            double elapsedMs = -1.0;
            bool llmAttempted = false;
            bool success = false;
        };

        void Tick(double gameMinutes, NextAI::FAIService* ai, AgentSystem& agents, AirportMap& map,
                  JourneySystem& journey, bool isNight);
        void Reset();

        bool InFlight() const { return inFlight_; }
        double InFlightElapsedMs() const;
        int DecisionsMade() const { return decisionsMade_; }
        int FallbacksUsed() const { return fallbacksUsed_; }
        const std::vector<FDecisionLogEntry>& Log() const { return log_; }

    private:
        struct FPendingResult
        {
            int agentId = -1;
            FDecisionResult result;
            double elapsedMs = 0.0;
            bool serviceSuccess = false;
            std::string errorMessage;
            std::string prompt;
            std::string response;
        };

        void ApplyResult(FAgent& agent, const FDecisionResult& result, double gameMinutes, AgentSystem& agents,
                         AirportMap& map, JourneySystem& journey, double llmElapsedMs, std::string prompt,
                         std::string response);
        void ApplyFallback(FAgent& agent, double gameMinutes, AgentSystem& agents, AirportMap& map,
                           JourneySystem& journey, bool isNight, double llmElapsedMs = -1.0,
                           std::string fallbackReason = {}, std::string prompt = {}, std::string response = {});
        void PushLog(FAgent& agent, double gameMinutes, std::string summary, std::string prompt = {},
                     std::string response = {}, double elapsedMs = -1.0, bool llmAttempted = false,
                     bool success = false);

        std::mutex mutex_;                      // 保护 completed_
        std::vector<FPendingResult> completed_; // worker push，主线程 drain
        bool inFlight_ = false;                 // 仅主线程读写
        double inFlightSince_ = 0.0;
        std::chrono::steady_clock::time_point inFlightStartedAt_{};
        int inFlightAgentId_ = -1;
        std::string inFlightPrompt_;
        int decisionsMade_ = 0;
        int fallbacksUsed_ = 0;
        uint64_t generation_ = 0;
        uint64_t nextLogId_ = 1;
        size_t scanCursor_ = 0;                 // 轮询起点：消除"池下标小的人永远先说话"的偏置
        std::mt19937 rng_{99};
        std::vector<FDecisionLogEntry> log_;    // 调试面板决策流水及原始请求/响应
    };
}
