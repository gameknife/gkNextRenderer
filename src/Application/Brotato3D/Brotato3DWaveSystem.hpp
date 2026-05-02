#pragma once

#include "Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"

#include <glm/ext.hpp>
#include <random>
#include <utility>

namespace Brotato3D
{
    enum class EWaveState : uint8_t
    {
        Idle,
        Active,
        Intermission,
        AllCleared,
    };

    class FWaveSystem
    {
    public:
        void LoadWaves(std::vector<FWaveDef> waves);
        void StartGame();
        void Reset();
        void Update(double dt, const std::function<void(const std::string& enemyId, glm::vec3 pos)>& spawnCallback);
        EWaveState GetState() const { return state_; }
        int GetCurrentWaveIndex() const { return currentWaveIndex_; }
        int GetWaveCount() const { return static_cast<int>(waves_.size()); }
        const FWaveDef* GetCurrentWaveDef() const;
        float GetWaveTimeRemainingSec() const { return waveTimeRemainingSec_; }
        float GetIntermissionTimeRemainingSec() const { return intermissionTimeRemainingSec_; }
        bool ConsumeWaveEnded() { return std::exchange(waveEndedEvent_, false); }
        bool ConsumeIntermissionStarted() { return std::exchange(intermissionStartedEvent_, false); }
        bool ConsumeVictory() { return std::exchange(victoryEvent_, false); }
        void EnterShop();
        void EndIntermissionAndAdvance();
        void ForceAllCleared();

    private:
        struct FSpawnRuntime
        {
            int spawnedCount = 0;
            float nextSpawnTimerMs = 0.0f;
        };

        void ResetSpawnRuntime();
        glm::vec3 RandomSpawnPosition();

        std::vector<FWaveDef> waves_;
        std::vector<FSpawnRuntime> spawnRuntime_;
        EWaveState state_ = EWaveState::Idle;
        int currentWaveIndex_ = 0;
        float waveTimeRemainingSec_ = 0.0f;
        float intermissionTimeRemainingSec_ = 0.0f;
        std::mt19937 rng_{std::random_device{}()};
        bool waveEndedEvent_ = false;
        bool intermissionStartedEvent_ = false;
        bool victoryEvent_ = false;
    };
}
