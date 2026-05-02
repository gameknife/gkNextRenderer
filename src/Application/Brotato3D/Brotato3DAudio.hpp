#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"

#include <chrono>
#include <spdlog/spdlog.h>

namespace Brotato3D
{
    inline float SfxVolume = 0.7f;
    inline float MusicVolume = 0.5f;
    inline bool ScreenShakeEnabled = true;
    inline bool ShowEnemyHpBars = true;
    inline float MasterDifficulty = 1.0f;

    inline std::string& CurrentBgmPath()
    {
        static std::string path;
        return path;
    }

    inline void PlayBrotatoSfx(const std::string& soundPath, float volumeScale = 1.0f, uint64_t minIntervalMs = 70)
    {
        static std::unordered_map<std::string, uint64_t> lastPlayMsBySound;
        static std::unordered_set<std::string> missingSounds;

        using namespace std::chrono;
        const uint64_t nowMs = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        const auto it = lastPlayMsBySound.find(soundPath);
        if (it != lastPlayMsBySound.end() && nowMs - it->second < minIntervalMs)
        {
            return;
        }

        lastPlayMsBySound[soundPath] = nowMs;
        if (!std::filesystem::exists(soundPath))
        {
            if (missingSounds.insert(soundPath).second)
            {
                spdlog::warn("[Brotato3D] missing sound '{}'", soundPath);
            }
            return;
        }

        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->PlaySound(soundPath, false, std::clamp(SfxVolume * volumeScale, 0.0f, 1.0f));
        }
    }

    inline void PlayWeaponFireSfx(const std::string& weaponId)
    {
        static const std::unordered_map<std::string, std::string> paths = {
            {"smg", "assets/sounds/brotato3d/fire_smg.wav"},
            {"shotgun", "assets/sounds/brotato3d/fire_shotgun.wav"},
            {"sniper", "assets/sounds/brotato3d/fire_sniper.wav"},
            {"flamethrower", "assets/sounds/brotato3d/fire_flamethrower.wav"},
            {"rocket", "assets/sounds/brotato3d/fire_rocket.wav"},
            {"laser", "assets/sounds/brotato3d/fire_laser.wav"},
        };
        const auto it = paths.find(weaponId);
        PlayBrotatoSfx(it != paths.end() ? it->second : "assets/sounds/brotato3d/fire_smg.wav", 0.75f, 70);
    }

    inline void PlayHitSfx(int damage, bool isCrit)
    {
        PlayBrotatoSfx(isCrit ? "assets/sounds/brotato3d/hit_crit.wav" : "assets/sounds/brotato3d/hit_normal.wav",
                       isCrit ? 0.85f : std::clamp(static_cast<float>(damage) / 20.0f, 0.45f, 0.8f),
                       isCrit ? 45 : 35);
    }

    inline void PlayPickupXpSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/pickup_xp.wav", 0.6f, 45);
    }

    inline void PlayPickupMaterialSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/pickup_material.wav", 0.65f, 45);
    }

    inline void PlayLevelUpSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/level_up.wav", 0.9f, 0);
    }

    inline void PlayWaveStartSfx(int waveIndex)
    {
        PlayBrotatoSfx(waveIndex >= 9 ? "assets/sounds/brotato3d/wave_start_boss.wav" :
                                        "assets/sounds/brotato3d/wave_start.wav",
                       0.85f,
                       0);
    }

    inline void PlayPlayerHurtSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/player_hurt.wav", 0.8f, 120);
    }

    inline void PlayEnemyDeathSfx(const std::string& enemyId)
    {
        if (enemyId == "Warden" || enemyId == "boss_warden")
        {
            PlayBrotatoSfx("assets/sounds/brotato3d/enemy_die_boss.wav", 1.0f, 0);
        }
        else if (enemyId == "Brute" || enemyId == "tank")
        {
            PlayBrotatoSfx("assets/sounds/brotato3d/enemy_die_tank.wav", 0.85f, 80);
        }
        else
        {
            PlayBrotatoSfx("assets/sounds/brotato3d/enemy_die_small.wav", 0.65f, 55);
        }
    }

    inline void PlayShopOpenSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/shop_open.wav", 0.75f, 0);
    }

    inline void PlayShopBuySfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/shop_buy.wav", 0.8f, 45);
    }

    inline void PlayUiClickSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/ui_click.wav", 0.55f, 35);
    }

    inline void PlayVictorySfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/victory.wav", 0.9f, 0);
    }

    inline void PlayDefeatSfx()
    {
        PlayBrotatoSfx("assets/sounds/brotato3d/defeat.wav", 0.9f, 0);
    }

    inline void StopBgm()
    {
        std::string& currentPath = CurrentBgmPath();
        if (currentPath.empty())
        {
            return;
        }
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->PauseSound(currentPath, true);
        }
        currentPath.clear();
    }

    inline void StartBgm(const std::string& trackName)
    {
        const std::string path = fmt::format("assets/sounds/brotato3d/bgm_{}.wav", trackName);
        std::string& currentPath = CurrentBgmPath();
        if (currentPath == path)
        {
            return;
        }

        StopBgm();
        currentPath = path;
        if (!std::filesystem::exists(currentPath))
        {
            spdlog::warn("[Brotato3D] missing bgm '{}'", currentPath);
            currentPath.clear();
            return;
        }

        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->PlaySound(currentPath, true, std::clamp(MusicVolume, 0.0f, 1.0f));
        }
    }

    inline void RefreshBgmVolume()
    {
        std::string& currentPath = CurrentBgmPath();
        if (currentPath.empty() || !std::filesystem::exists(currentPath))
        {
            return;
        }
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->PauseSound(currentPath, true);
            engine->PlaySound(currentPath, true, std::clamp(MusicVolume, 0.0f, 1.0f));
        }
    }
}
