#pragma once

// ============================================================================
// LevelFlow.hpp - Title -> Intro -> Playing -> Dead -> Goal -> Result, plus the
// run tally the HUD and the result screen read. Pure logic with no engine
// dependency so it can be unit tested (Test_AstroLevelFlow.cpp); the game
// instance is responsible for pausing physics outside Playing.
// ============================================================================

#include <cstdint>

namespace NextAstrobot
{
    enum class ELevelState : uint8_t
    {
        Title,
        Intro,
        Playing,
        Dead,
        Goal,
        Result,
        Paused,
    };

    const char* LevelStateName(ELevelState state);

    struct FRunStats
    {
        int coins = 0;
        int coinsTotal = 0;
        int puzzles = 0;
        int puzzlesTotal = 0;
        int rescued = 0;
        int rescuedTotal = 0;
        int gems = 0;
        int deaths = 0;
        double elapsedSeconds = 0.0;
    };

    class FLevelFlow
    {
    public:
        /// hasIntroPath tells the flow whether the level ships a camera track to fly.
        void Reset(bool hasIntroPath, float introSeconds, float deathSeconds, float goalSeconds);

        void Update(double deltaSeconds);

        /// Any key/button while in Title or Intro. Returns true when it consumed the press.
        bool RequestSkip();
        void RequestPause(bool paused);
        void NotifyDeath();
        void NotifyGoalReached();
        /// Called once the respawn has been applied, to leave the Dead state.
        void NotifyRespawned();

        ELevelState State() const { return state_; }
        bool IsPlaying() const { return state_ == ELevelState::Playing; }
        /// Physics and gameplay only advance while the world is live.
        bool WorldRunning() const { return state_ == ELevelState::Playing || state_ == ELevelState::Dead; }
        float StateElapsed() const { return stateElapsed_; }
        float IntroProgress01() const;
        /// 0 -> fully visible, 1 -> fully black; drives the death fade.
        float DeathFade01() const;

        FRunStats& Stats() { return stats_; }
        const FRunStats& Stats() const { return stats_; }

    private:
        void Enter(ELevelState state);

        ELevelState state_ = ELevelState::Title;
        ELevelState statePriorToPause_ = ELevelState::Playing;
        float stateElapsed_ = 0.0f;
        float introSeconds_ = 8.0f;
        float deathSeconds_ = 0.8f;
        float goalSeconds_ = 2.0f;
        bool hasIntroPath_ = false;
        bool respawnConsumed_ = false;
        FRunStats stats_{};
    };
}
