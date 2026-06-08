#pragma once

#include "StudioSimTypes.h"

#include <string>
#include <vector>

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
                  const FGameProject& gameProject);

        void AcceptDecision(int gatheringId, double gameMinutes, std::vector<FEmployee>& employees,
                            ProductionSystem& production);
        void RejectDecision(int gatheringId, double gameMinutes, std::vector<FEmployee>& employees);

        bool HasActiveMeeting() const;
        const std::vector<FGathering>& Gatherings() const { return gatherings_; }

    private:
        void StartGathering(EGatheringKind kind, const std::string& topic, double gameMinutes,
                            std::vector<FEmployee>& employees, const OfficeMap& office, const FGameProject& gameProject);
        void EvaluateTriggers(const FWorldState& world, std::vector<FEmployee>& employees, const OfficeMap& office,
                              const FGameProject& gameProject);
        void ReleaseGathering(FGathering& gathering, double gameMinutes, std::vector<FEmployee>& employees);

        std::vector<FGathering> gatherings_;
        std::vector<std::string> pendingMeetingTopics_;
        double nextEvalGameMinutes_ = 0.0;
        int nextId_ = 1;
    };
}
