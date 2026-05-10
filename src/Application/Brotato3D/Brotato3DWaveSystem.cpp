#include "Brotato3DWaveSystem.hpp"

#include <spdlog/spdlog.h>

namespace Brotato3D
{
    namespace
    {
        constexpr float SpawnIntervalScaleStart = 1.15f;
        constexpr float SpawnIntervalScaleEnd = 0.45f;
        constexpr float SpawnIntervalMinMs = 80.0f;

        float CalculateSpawnIntervalMs(const FWaveDef& wave,
                                       const FSpawnEntry& entry,
                                       float waveTimeRemainingSec,
                                       EWaveState state)
        {
            const float durationSec = std::max(1.0f, static_cast<float>(wave.durationSec));
            const float progress = 1.0f - std::clamp(waveTimeRemainingSec / durationSec, 0.0f, 1.0f);
            const float pressure = std::pow(progress, 1.25f);
            const float scale = glm::mix(SpawnIntervalScaleStart, SpawnIntervalScaleEnd, pressure);
            const float duskMultiplier = state == EWaveState::DuskSurge ? std::max(1.0f, wave.duskSpawnMultiplier) : 1.0f;
            return std::max(SpawnIntervalMinMs, entry.intervalMs * scale / duskMultiplier);
        }
    }

    void FWaveSystem::LoadWaves(std::vector<FWaveDef> waves)
    {
        waves_ = std::move(waves);
        Reset();
    }

    void FWaveSystem::StartGame()
    {
        if (waves_.empty())
        {
            state_ = EWaveState::AllCleared;
            victoryEvent_ = true;
            return;
        }

        currentWaveIndex_ = 0;
        waveTimeRemainingSec_ = static_cast<float>(waves_[currentWaveIndex_].durationSec);
        intermissionTimeRemainingSec_ = 0.0f;
        extractionElapsedSec_ = 0.0f;
        playerInExtractionZone_ = false;
        state_ = EWaveState::Active;
        ResetSpawnRuntime();
        spdlog::info("[Brotato3D] [Wave 1 start]");
    }

    void FWaveSystem::Reset()
    {
        state_ = EWaveState::Idle;
        currentWaveIndex_ = 0;
        waveTimeRemainingSec_ = 0.0f;
        intermissionTimeRemainingSec_ = 0.0f;
        extractionElapsedSec_ = 0.0f;
        playerInExtractionZone_ = false;
        waveEndedEvent_ = false;
        intermissionStartedEvent_ = false;
        duskBeganEvent_ = false;
        extractionCompletedEvent_ = false;
        victoryEvent_ = false;
        spawnRuntime_.clear();
    }

    void FWaveSystem::SetArenaHalfExtent(const glm::vec2& halfExtent)
    {
        arenaHalfExtent_ = glm::max(halfExtent, glm::vec2(1.0f));
    }

    void FWaveSystem::Update(double dt, const std::function<void(const std::string& enemyId, glm::vec3 pos)>& spawnCallback)
    {
        if (state_ == EWaveState::Active)
        {
            if (currentWaveIndex_ < 0 || currentWaveIndex_ >= static_cast<int>(waves_.size()))
            {
                state_ = EWaveState::AllCleared;
                victoryEvent_ = true;
                return;
            }

            const float deltaMs = static_cast<float>(dt * 1000.0);
            const FWaveDef& wave = waves_[currentWaveIndex_];
            UpdateSpawns(deltaMs, spawnCallback);

            waveTimeRemainingSec_ -= static_cast<float>(dt);
            if (waveTimeRemainingSec_ <= 0.0f)
            {
                if (wave.bgmCue == "boss")
                {
                    waveTimeRemainingSec_ = 0.0f;
                    return;
                }
                if (ShouldEnterDuskSurge(wave))
                {
                    state_ = EWaveState::DuskSurge;
                    waveTimeRemainingSec_ = 0.0f;
                    extractionElapsedSec_ = 0.0f;
                    playerInExtractionZone_ = false;
                    duskBeganEvent_ = true;
                    spdlog::info("[Brotato3D] [Dusk surge start wave {}]", currentWaveIndex_ + 1);
                    return;
                }
                spdlog::info("[Brotato3D] [Wave {} ended]", currentWaveIndex_ + 1);
                waveEndedEvent_ = true;
                EnterShop();
            }
        }
        else if (state_ == EWaveState::DuskSurge)
        {
            if (currentWaveIndex_ < 0 || currentWaveIndex_ >= static_cast<int>(waves_.size()))
            {
                state_ = EWaveState::AllCleared;
                victoryEvent_ = true;
                return;
            }

            const float deltaMs = static_cast<float>(dt * 1000.0);
            const FWaveDef& wave = waves_[currentWaveIndex_];
            UpdateSpawns(deltaMs, spawnCallback);
            if (playerInExtractionZone_)
            {
                extractionElapsedSec_ += static_cast<float>(dt);
            }
            if (extractionElapsedSec_ >= std::max(0.1f, wave.extractionRequiredSec))
            {
                extractionElapsedSec_ = wave.extractionRequiredSec;
                extractionCompletedEvent_ = true;
                waveEndedEvent_ = true;
                spdlog::info("[Brotato3D] [Wave {} extracted]", currentWaveIndex_ + 1);
                EnterShop();
            }
        }
        else if (state_ == EWaveState::Intermission)
        {
            intermissionTimeRemainingSec_ -= static_cast<float>(dt);
            if (intermissionTimeRemainingSec_ <= 0.0f)
            {
                EndIntermissionAndAdvance();
            }
        }
    }

    void FWaveSystem::EnterShop()
    {
        state_ = EWaveState::Intermission;
        intermissionTimeRemainingSec_ = 5.0f;
        playerInExtractionZone_ = false;
        intermissionStartedEvent_ = true;
    }

    void FWaveSystem::EndIntermissionAndAdvance()
    {
        ++currentWaveIndex_;
        if (currentWaveIndex_ >= static_cast<int>(waves_.size()))
        {
            state_ = EWaveState::AllCleared;
            victoryEvent_ = true;
            spdlog::info("[Brotato3D] [victory]");
            return;
        }

        waveTimeRemainingSec_ = static_cast<float>(waves_[currentWaveIndex_].durationSec);
        intermissionTimeRemainingSec_ = 0.0f;
        extractionElapsedSec_ = 0.0f;
        playerInExtractionZone_ = false;
        state_ = EWaveState::Active;
        ResetSpawnRuntime();
        spdlog::info("[Brotato3D] [Wave {} start]", currentWaveIndex_ + 1);
    }

    void FWaveSystem::ForceAllCleared()
    {
        state_ = EWaveState::AllCleared;
    }

    const FWaveDef* FWaveSystem::GetCurrentWaveDef() const
    {
        if (currentWaveIndex_ < 0 || currentWaveIndex_ >= static_cast<int>(waves_.size()))
        {
            return nullptr;
        }
        return &waves_[currentWaveIndex_];
    }

    float FWaveSystem::GetExtractionRequiredSec() const
    {
        const FWaveDef* wave = GetCurrentWaveDef();
        return wave ? wave->extractionRequiredSec : 0.0f;
    }

    float FWaveSystem::GetExtractionProgress() const
    {
        const float required = GetExtractionRequiredSec();
        if (required <= 0.0f)
        {
            return 0.0f;
        }
        return std::clamp(extractionElapsedSec_ / required, 0.0f, 1.0f);
    }

    void FWaveSystem::NotifyPlayerInExtractionZone(bool inZone)
    {
        playerInExtractionZone_ = inZone;
    }

    void FWaveSystem::ResetSpawnRuntime()
    {
        spawnRuntime_.clear();
        if (currentWaveIndex_ < 0 || currentWaveIndex_ >= static_cast<int>(waves_.size()))
        {
            return;
        }

        spawnRuntime_.resize(waves_[currentWaveIndex_].spawns.size());
    }

    void FWaveSystem::UpdateSpawns(float deltaMs, const std::function<void(const std::string& enemyId, glm::vec3 pos)>& spawnCallback)
    {
        const FWaveDef& wave = waves_[currentWaveIndex_];
        for (size_t index = 0; index < wave.spawns.size(); ++index)
        {
            FSpawnRuntime& runtime = spawnRuntime_[index];
            const FSpawnEntry& entry = wave.spawns[index];
            runtime.nextSpawnTimerMs -= deltaMs;
            const bool unlimitedDuskSpawn = state_ == EWaveState::DuskSurge;
            while ((unlimitedDuskSpawn || runtime.spawnedCount < entry.count) && runtime.nextSpawnTimerMs <= 0.0f)
            {
                spawnCallback(entry.enemyId, RandomSpawnPosition());
                ++runtime.spawnedCount;
                runtime.nextSpawnTimerMs += CalculateSpawnIntervalMs(wave, entry, waveTimeRemainingSec_, state_);
            }
        }
    }

    bool FWaveSystem::ShouldEnterDuskSurge(const FWaveDef& wave) const
    {
        return wave.bgmCue != "boss" &&
               wave.extractionRequiredSec > 0.0f;
    }

    glm::vec3 FWaveSystem::RandomSpawnPosition()
    {
        constexpr float SpawnMargin = 1.0f;
        std::uniform_int_distribution<int> sideDist(0, 3);
        std::uniform_real_distribution<float> xDist(-arenaHalfExtent_.x, arenaHalfExtent_.x);
        std::uniform_real_distribution<float> zDist(-arenaHalfExtent_.y, arenaHalfExtent_.y);

        switch (sideDist(rng_))
        {
        case 0:
            return glm::vec3(xDist(rng_), 0.3f, -(arenaHalfExtent_.y + SpawnMargin));
        case 1:
            return glm::vec3(xDist(rng_), 0.3f, arenaHalfExtent_.y + SpawnMargin);
        case 2:
            return glm::vec3(-(arenaHalfExtent_.x + SpawnMargin), 0.3f, zDist(rng_));
        default:
            return glm::vec3(arenaHalfExtent_.x + SpawnMargin, 0.3f, zDist(rng_));
        }
    }
}
