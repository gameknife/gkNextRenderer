#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Brotato3DAssetPaths.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"

#include <spdlog/spdlog.h>

namespace Brotato3D
{
    inline float SfxVolume = 0.7f;
    inline float MusicVolume = 0.5f;
    inline bool ScreenShakeEnabled = true;
    inline bool ShowEnemyHpBars = true;
    inline float MasterDifficulty = 1.0f;

    inline void PlayBrotatoSfx(const std::string& soundPath, float volumeScale = 1.0f, uint64_t minIntervalMs = 70)
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextAudio* audio = engine->GetAudio())
            {
                audio->PlaySfx(soundPath, std::clamp(SfxVolume * volumeScale, 0.0f, 1.0f), minIntervalMs);
            }
        }
    }

    inline void PlayBrotatoSfxVariant(std::initializer_list<std::string_view> candidates,
                                      float volumeScale = 1.0f,
                                      uint64_t minIntervalMs = 70)
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextAudio* audio = engine->GetAudio())
            {
                audio->PlaySfxVariant(candidates, std::clamp(SfxVolume * volumeScale, 0.0f, 1.0f), minIntervalMs);
            }
        }
    }

    inline void PlayWeaponFireSfx(const std::string& weaponId)
    {
        if (weaponId == "smg")
        {
            PlayBrotatoSfxVariant({
                               Assets::Sfx("fire_smg_01.wav"),
                               Assets::Sfx("fire_smg_02.wav"),
                               Assets::Sfx("fire_smg_03.wav"),
                               Assets::Sfx("fire_smg_04.wav"),
                               Assets::Sfx("fire_smg_05.wav"),
                               Assets::Sfx("fire_smg_06.wav"),
                               Assets::Sfx("fire_smg_07.wav"),
                               Assets::Sfx("fire_smg_08.wav"),
                               Assets::Sfx("fire_smg_09.wav"),
                           },
                           0.75f,
                           55);
            return;
        }
        if (weaponId == "flamethrower")
        {
            PlayBrotatoSfxVariant({
                               Assets::Sfx("fire_flamethrower_01.wav"),
                               Assets::Sfx("fire_flamethrower_02.wav"),
                               Assets::Sfx("fire_flamethrower_03.wav"),
                               Assets::Sfx("fire_flamethrower_04.wav"),
                           },
                           0.72f,
                           40);
            return;
        }

        static const std::unordered_map<std::string, std::string> paths = {
            {"shotgun", Assets::Sfx("fire_shotgun_01.wav")},
            {"sniper", Assets::Sfx("fire_sniper_01.wav")},
            {"rocket", Assets::Sfx("fire_rocket_01.wav")},
            {"laser", Assets::Sfx("fire_laser_01.wav")},
        };
        const auto it = paths.find(weaponId);
        PlayBrotatoSfx(it != paths.end() ? it->second : Assets::Sfx("fire_smg_01.wav"), 0.75f, 70);
    }

    inline void PlayHitSfx(int damage, bool isCrit)
    {
        if (isCrit)
        {
            PlayBrotatoSfxVariant({
                                    Assets::Sfx("hit_crit_01.wav"),
                                    Assets::Sfx("hit_crit_02.wav"),
                                    Assets::Sfx("hit_crit_03.wav"),
                                    Assets::Sfx("hit_crit_04.wav"),
                                },
                                0.85f,
                                45);
        }
        else
        {
            PlayBrotatoSfxVariant({
                                    Assets::Sfx("hit_normal_01.wav"),
                                    Assets::Sfx("hit_normal_02.wav"),
                                    Assets::Sfx("hit_normal_03.wav"),
                                    Assets::Sfx("hit_normal_04.wav"),
                                    Assets::Sfx("hit_normal_05.wav"),
                                },
                                std::clamp(static_cast<float>(damage) / 20.0f, 0.45f, 0.8f),
                                35);
        }
    }

    inline void PlayPickupXpSfx()
    {
        PlayBrotatoSfxVariant({
                           Assets::Sfx("pickup_xp_01.ogg"),
                           Assets::Sfx("pickup_xp_02.ogg"),
                       },
                       0.6f,
                       45);
    }

    inline void PlayPickupMaterialSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("pickup_material.wav"), 0.65f, 45);
    }

    inline void PlayLevelUpSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("level_up.wav"), 0.9f, 0);
    }

    inline void PlayWaveStartSfx(int waveIndex)
    {
        PlayBrotatoSfx(waveIndex >= 9 ? Assets::Sfx("wave_start_boss.wav") : Assets::Sfx("wave_start.wav"),
                       0.85f,
                       0);
    }

    inline void PlayPlayerHurtSfx()
    {
        PlayBrotatoSfxVariant({
                           Assets::Sfx("player_hurt_01.wav"),
                           Assets::Sfx("player_hurt_02.wav"),
                           Assets::Sfx("player_hurt_03.wav"),
                           Assets::Sfx("player_hurt_04.wav"),
                       },
                       0.8f,
                       120);
    }

    inline void PlayEnemyDeathSfx(const std::string& enemyId)
    {
        if (enemyId == "Warden" || enemyId == "boss_warden")
        {
            PlayBrotatoSfx(Assets::Sfx("enemy_die_boss.wav"), 1.0f, 0);
        }
        else if (enemyId == "Brute" || enemyId == "tank")
        {
            PlayBrotatoSfxVariant({
                               Assets::Sfx("enemy_die_tank_01.wav"),
                               Assets::Sfx("enemy_die_tank_02.wav"),
                               Assets::Sfx("enemy_die_tank_03.wav"),
                           },
                           0.85f,
                           80);
        }
        else
        {
            PlayBrotatoSfxVariant({
                               Assets::Sfx("enemy_die_small_01.wav"),
                               Assets::Sfx("enemy_die_small_02.wav"),
                           },
                           0.65f,
                           55);
        }
    }

    inline void PlayShopOpenSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("shop_open.wav"), 0.75f, 0);
    }

    inline void PlayShopBuySfx()
    {
        PlayBrotatoSfx(Assets::Sfx("shop_buy.wav"), 0.8f, 45);
    }

    inline void PlayShopRerollSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("shop_reroll.wav"), 0.75f, 45);
    }

    inline void PlayShopCantBuySfx()
    {
        PlayBrotatoSfx(Assets::Sfx("shop_cant_buy.wav"), 0.75f, 45);
    }

    inline void PlayUiClickSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("ui_click.wav"), 0.55f, 35);
    }

    inline void PlayUiHoverSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("ui_hover.wav"), 0.45f, 25);
    }

    inline void PlayVictorySfx()
    {
        PlayBrotatoSfx(Assets::Sfx("victory.wav"), 0.9f, 0);
    }

    inline void PlayDefeatSfx()
    {
        PlayBrotatoSfx(Assets::Sfx("defeat.wav"), 0.9f, 0);
    }

    inline void StopBgm()
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextAudio* audio = engine->GetAudio())
            {
                audio->StopMusic();
            }
        }
    }

    inline void StartBgm(const std::string& trackName)
    {
        const std::string path = PlaceholderAssets::Bgm(fmt::format("bgm_{}.mp3", trackName));
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextAudio* audio = engine->GetAudio())
            {
                audio->PlayMusic(path, std::clamp(MusicVolume, 0.0f, 1.0f));
            }
        }
    }

    inline void RefreshBgmVolume()
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextAudio* audio = engine->GetAudio())
            {
                audio->SetMusicVolume(std::clamp(MusicVolume, 0.0f, 1.0f));
            }
        }
    }
}
