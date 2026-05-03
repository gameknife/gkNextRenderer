#pragma once

#include "Common/CoreMinimal.hpp"
#include "Brotato3DAssetPaths.hpp"
#include "Runtime/Engine.hpp"

#include <chrono>
#include <random>
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

    inline std::mt19937& AudioRng()
    {
        static std::mt19937 rng(std::random_device{}());
        return rng;
    }

    inline std::string PickVariantPath(std::initializer_list<std::string> candidates)
    {
        if (candidates.size() == 0)
        {
            return {};
        }
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        auto it = candidates.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(dist(AudioRng())));
        return *it;
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
        if (weaponId == "smg")
        {
            PlayBrotatoSfx(PickVariantPath({
                               PlaceholderAssets::Sfx("fire_smg_01.wav"),
                               PlaceholderAssets::Sfx("fire_smg_02.wav"),
                               PlaceholderAssets::Sfx("fire_smg_03.wav"),
                               PlaceholderAssets::Sfx("fire_smg_04.wav"),
                               PlaceholderAssets::Sfx("fire_smg_05.wav"),
                               PlaceholderAssets::Sfx("fire_smg_06.wav"),
                               PlaceholderAssets::Sfx("fire_smg_07.wav"),
                               PlaceholderAssets::Sfx("fire_smg_08.wav"),
                               PlaceholderAssets::Sfx("fire_smg_09.wav"),
                           }),
                           0.75f,
                           55);
            return;
        }
        if (weaponId == "flamethrower")
        {
            PlayBrotatoSfx(PickVariantPath({
                               PlaceholderAssets::Sfx("fire_flamethrower_01.wav"),
                               PlaceholderAssets::Sfx("fire_flamethrower_02.wav"),
                               PlaceholderAssets::Sfx("fire_flamethrower_03.wav"),
                               PlaceholderAssets::Sfx("fire_flamethrower_04.wav"),
                           }),
                           0.72f,
                           40);
            return;
        }

        static const std::unordered_map<std::string, std::string> paths = {
            {"shotgun", PlaceholderAssets::Sfx("fire_shotgun_01.wav")},
            {"sniper", PlaceholderAssets::Sfx("fire_sniper_01.wav")},
            {"rocket", PlaceholderAssets::Sfx("fire_rocket_01.wav")},
            {"laser", PlaceholderAssets::Sfx("fire_laser_01.wav")},
        };
        const auto it = paths.find(weaponId);
        PlayBrotatoSfx(it != paths.end() ? it->second : PlaceholderAssets::Sfx("fire_smg_01.wav"), 0.75f, 70);
    }

    inline void PlayHitSfx(int damage, bool isCrit)
    {
        PlayBrotatoSfx(isCrit ? PickVariantPath({
                                    PlaceholderAssets::Sfx("hit_crit_01.wav"),
                                    PlaceholderAssets::Sfx("hit_crit_02.wav"),
                                    PlaceholderAssets::Sfx("hit_crit_03.wav"),
                                    PlaceholderAssets::Sfx("hit_crit_04.wav"),
                                }) :
                                PickVariantPath({
                                    PlaceholderAssets::Sfx("hit_normal_01.wav"),
                                    PlaceholderAssets::Sfx("hit_normal_02.wav"),
                                    PlaceholderAssets::Sfx("hit_normal_03.wav"),
                                    PlaceholderAssets::Sfx("hit_normal_04.wav"),
                                    PlaceholderAssets::Sfx("hit_normal_05.wav"),
                                }),
                       isCrit ? 0.85f : std::clamp(static_cast<float>(damage) / 20.0f, 0.45f, 0.8f),
                       isCrit ? 45 : 35);
    }

    inline void PlayPickupXpSfx()
    {
        PlayBrotatoSfx(PickVariantPath({
                           PlaceholderAssets::Sfx("pickup_xp_01.ogg"),
                           PlaceholderAssets::Sfx("pickup_xp_02.ogg"),
                       }),
                       0.6f,
                       45);
    }

    inline void PlayPickupMaterialSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("pickup_material.wav"), 0.65f, 45);
    }

    inline void PlayLevelUpSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("level_up.wav"), 0.9f, 0);
    }

    inline void PlayWaveStartSfx(int waveIndex)
    {
        PlayBrotatoSfx(waveIndex >= 9 ? PlaceholderAssets::Sfx("wave_start_boss.wav") : PlaceholderAssets::Sfx("wave_start.wav"),
                       0.85f,
                       0);
    }

    inline void PlayPlayerHurtSfx()
    {
        PlayBrotatoSfx(PickVariantPath({
                           PlaceholderAssets::Sfx("player_hurt_01.wav"),
                           PlaceholderAssets::Sfx("player_hurt_02.wav"),
                           PlaceholderAssets::Sfx("player_hurt_03.wav"),
                           PlaceholderAssets::Sfx("player_hurt_04.wav"),
                       }),
                       0.8f,
                       120);
    }

    inline void PlayEnemyDeathSfx(const std::string& enemyId)
    {
        if (enemyId == "Warden" || enemyId == "boss_warden")
        {
            PlayBrotatoSfx(PlaceholderAssets::Sfx("enemy_die_boss.wav"), 1.0f, 0);
        }
        else if (enemyId == "Brute" || enemyId == "tank")
        {
            PlayBrotatoSfx(PickVariantPath({
                               PlaceholderAssets::Sfx("enemy_die_tank_01.wav"),
                               PlaceholderAssets::Sfx("enemy_die_tank_02.wav"),
                               PlaceholderAssets::Sfx("enemy_die_tank_03.wav"),
                           }),
                           0.85f,
                           80);
        }
        else
        {
            PlayBrotatoSfx(PickVariantPath({
                               PlaceholderAssets::Sfx("enemy_die_small_01.wav"),
                               PlaceholderAssets::Sfx("enemy_die_small_02.wav"),
                           }),
                           0.65f,
                           55);
        }
    }

    inline void PlayShopOpenSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("shop_open.wav"), 0.75f, 0);
    }

    inline void PlayShopBuySfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("shop_buy.wav"), 0.8f, 45);
    }

    inline void PlayShopRerollSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("shop_reroll.wav"), 0.75f, 45);
    }

    inline void PlayShopCantBuySfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("shop_cant_buy.wav"), 0.75f, 45);
    }

    inline void PlayUiClickSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("ui_click.wav"), 0.55f, 35);
    }

    inline void PlayUiHoverSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("ui_hover.wav"), 0.45f, 25);
    }

    inline void PlayVictorySfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("victory.wav"), 0.9f, 0);
    }

    inline void PlayDefeatSfx()
    {
        PlayBrotatoSfx(PlaceholderAssets::Sfx("defeat.wav"), 0.9f, 0);
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
        const std::string path = PlaceholderAssets::Bgm(fmt::format("bgm_{}.mp3", trackName));
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
