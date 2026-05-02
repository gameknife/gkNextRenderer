#include "Brotato3DWaveSystem.hpp"

#include <spdlog/spdlog.h>

namespace Brotato3D
{
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
        waveEndedEvent_ = false;
        intermissionStartedEvent_ = false;
        victoryEvent_ = false;
        spawnRuntime_.clear();
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
            for (size_t index = 0; index < wave.spawns.size(); ++index)
            {
                FSpawnRuntime& runtime = spawnRuntime_[index];
                const FSpawnEntry& entry = wave.spawns[index];
                runtime.nextSpawnTimerMs -= deltaMs;
                while (runtime.spawnedCount < entry.count && runtime.nextSpawnTimerMs <= 0.0f)
                {
                    spawnCallback(entry.enemyId, RandomSpawnPosition());
                    ++runtime.spawnedCount;
                    runtime.nextSpawnTimerMs += entry.intervalMs;
                }
            }

            waveTimeRemainingSec_ -= static_cast<float>(dt);
            if (waveTimeRemainingSec_ <= 0.0f)
            {
                if (wave.bgmCue == "boss")
                {
                    waveTimeRemainingSec_ = 0.0f;
                    return;
                }
                spdlog::info("[Brotato3D] [Wave {} ended]", currentWaveIndex_ + 1);
                waveEndedEvent_ = true;
                if (currentWaveIndex_ + 1 >= static_cast<int>(waves_.size()))
                {
                    state_ = EWaveState::AllCleared;
                    victoryEvent_ = true;
                    spdlog::info("[Brotato3D] [victory]");
                    return;
                }
                spdlog::info("[Brotato3D] [Intermission start]");
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

    void FWaveSystem::ResetSpawnRuntime()
    {
        spawnRuntime_.clear();
        if (currentWaveIndex_ < 0 || currentWaveIndex_ >= static_cast<int>(waves_.size()))
        {
            return;
        }

        spawnRuntime_.resize(waves_[currentWaveIndex_].spawns.size());
    }

    glm::vec3 FWaveSystem::RandomSpawnPosition()
    {
        std::uniform_int_distribution<int> sideDist(0, 3);
        std::uniform_real_distribution<float> xDist(-12.0f, 12.0f);
        std::uniform_real_distribution<float> zDist(-8.0f, 8.0f);

        switch (sideDist(rng_))
        {
        case 0:
            return glm::vec3(xDist(rng_), 0.3f, -9.0f);
        case 1:
            return glm::vec3(xDist(rng_), 0.3f, 9.0f);
        case 2:
            return glm::vec3(-13.0f, 0.3f, zDist(rng_));
        default:
            return glm::vec3(13.0f, 0.3f, zDist(rng_));
        }
    }
}
