#include "Battle/BattleSession.h"

#include <algorithm>

namespace NextTotalwar
{
    void FBattleSession::Reset(uint64_t seed, bool skipBriefing)
    {
        seed_ = seed ? seed : 1;
        ++generation_;
        battleSeconds_ = 0.0f;
        timeScale_ = 1.0f;
        result_ = EBattleResult::None;
        phase_ = skipBriefing ? EBattlePhase::Active : EBattlePhase::Briefing;
        phaseBeforePause_ = EBattlePhase::Active;
    }

    bool FBattleSession::BeginDeployment()
    {
        if (phase_ != EBattlePhase::Briefing) return false;
        phase_ = EBattlePhase::Deployment;
        return true;
    }

    bool FBattleSession::BeginBattle()
    {
        if (phase_ != EBattlePhase::Briefing && phase_ != EBattlePhase::Deployment) return false;
        phase_ = EBattlePhase::Active;
        return true;
    }

    bool FBattleSession::TogglePause()
    {
        if (phase_ == EBattlePhase::Active)
        {
            phaseBeforePause_ = phase_;
            phase_ = EBattlePhase::Paused;
            return true;
        }
        if (phase_ == EBattlePhase::Paused)
        {
            phase_ = phaseBeforePause_;
            return true;
        }
        return false;
    }

    void FBattleSession::SetTimeScale(float timeScale)
    {
        timeScale_ = std::clamp(timeScale, 0.25f, 4.0f);
    }

    void FBattleSession::Tick(float deltaSeconds, const std::vector<FRegiment>& regiments,
                              int playerFaction)
    {
        if (phase_ != EBattlePhase::Active) return;
        battleSeconds_ += std::max(deltaSeconds, 0.0f);
        int playerOperational = 0;
        int enemyOperational = 0;
        for (const FRegiment& regiment : regiments)
        {
            if (regiment.strength <= 0 || regiment.state == ERegimentState::Destroyed ||
                regiment.moraleState == EMoraleState::Eliminated ||
                regiment.moraleState == EMoraleState::Routing)
            {
                continue;
            }
            if (regiment.faction == playerFaction) ++playerOperational;
            else ++enemyOperational;
        }
        if (playerOperational > 0 && enemyOperational > 0) return;
        phase_ = EBattlePhase::Finished;
        if (playerOperational == 0 && enemyOperational == 0) result_ = EBattleResult::Draw;
        else result_ = playerOperational > 0 ? EBattleResult::Victory : EBattleResult::Defeat;
    }

    const char* FBattleSession::PhaseName(EBattlePhase phase)
    {
        switch (phase)
        {
        case EBattlePhase::Loading: return "Loading";
        case EBattlePhase::Briefing: return "Briefing";
        case EBattlePhase::Deployment: return "Deployment";
        case EBattlePhase::Active: return "Battle";
        case EBattlePhase::Paused: return "Paused";
        case EBattlePhase::Finished: return "Finished";
        }
        return "Unknown";
    }

    const char* FBattleSession::ResultName(EBattleResult result)
    {
        switch (result)
        {
        case EBattleResult::Victory: return "Victory";
        case EBattleResult::Defeat: return "Defeat";
        case EBattleResult::Draw: return "Draw";
        default: return "Undecided";
        }
    }
}
