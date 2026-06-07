#pragma once

#include "Engine/Common/CoreMinimal.hpp"
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
        DuskSurge,
        Intermission,
        AllCleared,
    };

    class FWaveSystem
    {
    public:
        void LoadWaves(std::vector<FWaveDef> waves);
        void StartGame();
        void Reset();
        void SetArenaHalfExtent(const glm::vec2& halfExtent);
        void Update(double dt, const std::function<void(const std::string& enemyId, glm::vec3 pos)>& spawnCallback);
        EWaveState GetState() const { return state_; }
        int GetCurrentWaveIndex() const { return currentWaveIndex_; }
        int GetWaveCount() const { return static_cast<int>(waves_.size()); }
        const FWaveDef* GetCurrentWaveDef() const;
        float GetWaveTimeRemainingSec() const { return waveTimeRemainingSec_; }
        float GetIntermissionTimeRemainingSec() const { return intermissionTimeRemainingSec_; }
        float GetExtractionElapsedSec() const { return extractionElapsedSec_; }
        float GetExtractionRequiredSec() const;
        float GetExtractionProgress() const;
        bool IsPlayerInExtractionZone() const { return playerInExtractionZone_; }
        bool ConsumeWaveEnded() { return std::exchange(waveEndedEvent_, false); }
        bool ConsumeIntermissionStarted() { return std::exchange(intermissionStartedEvent_, false); }
        bool ConsumeDuskBegan() { return std::exchange(duskBeganEvent_, false); }
        bool ConsumeExtractionCompleted() { return std::exchange(extractionCompletedEvent_, false); }
        bool ConsumeVictory() { return std::exchange(victoryEvent_, false); }
        void NotifyPlayerInExtractionZone(bool inZone);
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
        void UpdateSpawns(float deltaMs, const std::function<void(const std::string& enemyId, glm::vec3 pos)>& spawnCallback);
        bool ShouldEnterDuskSurge(const FWaveDef& wave) const;
        glm::vec3 RandomSpawnPosition();

        std::vector<FWaveDef> waves_;
        std::vector<FSpawnRuntime> spawnRuntime_;
        EWaveState state_ = EWaveState::Idle;
        int currentWaveIndex_ = 0;
        float waveTimeRemainingSec_ = 0.0f;
        float intermissionTimeRemainingSec_ = 0.0f;
        float extractionElapsedSec_ = 0.0f;
        bool playerInExtractionZone_ = false;
        glm::vec2 arenaHalfExtent_ = glm::vec2(12.0f, 8.0f);
        std::mt19937 rng_{std::random_device{}()};
        bool waveEndedEvent_ = false;
        bool intermissionStartedEvent_ = false;
        bool duskBeganEvent_ = false;
        bool extractionCompletedEvent_ = false;
        bool victoryEvent_ = false;
    };
}
