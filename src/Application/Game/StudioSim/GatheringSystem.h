#pragma once

#include "StudioSimTypes.h"

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
    class ProductionSystem;

    class GatheringSystem
    {
    public:
        void Reset();
        void RequestMeeting(const std::string& topic);
        void Tick(double deltaSeconds, FWorldState& world, std::vector<FEmployee>& employees, const OfficeMap& office,
                  const FGameProject& gameProject, NextAI::FAIService* ai);

        void AcceptDecision(int gatheringId, double gameMinutes, std::vector<FEmployee>& employees,
                            ProductionSystem& production);
        void RejectDecision(int gatheringId, double gameMinutes, std::vector<FEmployee>& employees);

        bool HasActiveMeeting() const;
        const std::vector<FGathering>& Gatherings() const { return gatherings_; }

    private:
        void StartGathering(EGatheringKind kind, const std::string& topic, double gameMinutes,
                            std::vector<FEmployee>& employees, const OfficeMap& office, const FGameProject& gameProject,
                            NextAI::FAIService* ai);
        void EvaluateTriggers(const FWorldState& world, std::vector<FEmployee>& employees, const OfficeMap& office,
                              const FGameProject& gameProject, NextAI::FAIService* ai);
        void ReleaseGathering(FGathering& gathering, double gameMinutes, std::vector<FEmployee>& employees);
        // 为一场聚集异步请求 LLM 生成"进度感知的多人对白 + 群体决策"，回来后在主线程 Tick 回灌。
        void RequestGatheringContent(FGathering& gathering, const std::vector<FEmployee>& employees,
                                     const FGameProject& gameProject, NextAI::FAIService* ai);

        // LLM 异步产物：键到具体某场聚集，主线程消费。
        struct FPendingContent
        {
            int id = -1;
            uint64_t generation = 0;
            std::vector<FMeetingLine> lines;
            FGroupDecision decision;
            bool hasDecision = false;
        };

        std::vector<FGathering> gatherings_;
        std::vector<std::string> pendingMeetingTopics_;
        double nextEvalGameMinutes_ = 0.0;
        int nextId_ = 1;

        std::mutex mutex_;
        uint64_t generation_ = 0;
        std::vector<FPendingContent> completed_;
    };
}
