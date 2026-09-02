#include "Application/Game/NextAstrobot/Level/LevelFlow.hpp"

#include <algorithm>

namespace NextAstrobot
{
    const char* LevelStateName(ELevelState state)
    {
        switch (state)
        {
        case ELevelState::Title: return "title";
        case ELevelState::Intro: return "intro";
        case ELevelState::Playing: return "playing";
        case ELevelState::Dead: return "dead";
        case ELevelState::Goal: return "goal";
        case ELevelState::Result: return "result";
        case ELevelState::Paused: return "paused";
        }
        return "unknown";
    }

    void FLevelFlow::Reset(bool hasIntroPath, float introSeconds, float deathSeconds, float goalSeconds)
    {
        hasIntroPath_ = hasIntroPath;
        introSeconds_ = std::max(0.1f, introSeconds);
        deathSeconds_ = std::max(0.05f, deathSeconds);
        goalSeconds_ = std::max(0.1f, goalSeconds);
        stats_ = FRunStats{};
        respawnConsumed_ = true;
        Enter(ELevelState::Title);
    }

    void FLevelFlow::Enter(ELevelState state)
    {
        state_ = state;
        stateElapsed_ = 0.0f;
    }

    void FLevelFlow::Update(double deltaSeconds)
    {
        const float dt = static_cast<float>(deltaSeconds);
        stateElapsed_ += dt;
        switch (state_)
        {
        case ELevelState::Playing:
            stats_.elapsedSeconds += deltaSeconds;
            break;
        case ELevelState::Intro:
            stats_.elapsedSeconds = 0.0;
            if (stateElapsed_ >= introSeconds_)
            {
                Enter(ELevelState::Playing);
            }
            break;
        case ELevelState::Dead:
            // The respawn is applied by the game once the fade has covered the screen;
            // NotifyRespawned then returns control to the player.
            break;
        case ELevelState::Goal:
            if (stateElapsed_ >= goalSeconds_)
            {
                Enter(ELevelState::Result);
            }
            break;
        case ELevelState::Title:
        case ELevelState::Result:
        case ELevelState::Paused:
            break;
        }
    }

    bool FLevelFlow::RequestSkip()
    {
        if (state_ == ELevelState::Title)
        {
            Enter(hasIntroPath_ ? ELevelState::Intro : ELevelState::Playing);
            return true;
        }
        if (state_ == ELevelState::Intro)
        {
            Enter(ELevelState::Playing);
            return true;
        }
        return false;
    }

    void FLevelFlow::RequestPause(bool paused)
    {
        if (paused && state_ != ELevelState::Paused)
        {
            statePriorToPause_ = state_;
            Enter(ELevelState::Paused);
        }
        else if (!paused && state_ == ELevelState::Paused)
        {
            Enter(statePriorToPause_);
        }
    }

    void FLevelFlow::NotifyDeath()
    {
        if (state_ != ELevelState::Playing)
        {
            return;
        }
        ++stats_.deaths;
        respawnConsumed_ = false;
        Enter(ELevelState::Dead);
    }

    void FLevelFlow::NotifyRespawned()
    {
        if (state_ != ELevelState::Dead)
        {
            return;
        }
        respawnConsumed_ = true;
        Enter(ELevelState::Playing);
    }

    void FLevelFlow::NotifyGoalReached()
    {
        if (state_ != ELevelState::Playing)
        {
            return;
        }
        Enter(ELevelState::Goal);
    }

    float FLevelFlow::IntroProgress01() const
    {
        if (state_ != ELevelState::Intro)
        {
            return 0.0f;
        }
        return std::clamp(stateElapsed_ / introSeconds_, 0.0f, 1.0f);
    }

    float FLevelFlow::DeathFade01() const
    {
        if (state_ != ELevelState::Dead)
        {
            return 0.0f;
        }
        // Fade to black over the first half, hold, then the respawn cuts back.
        return std::clamp(stateElapsed_ / (deathSeconds_ * 0.5f), 0.0f, 1.0f);
    }
}
