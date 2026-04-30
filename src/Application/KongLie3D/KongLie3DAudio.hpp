#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"

#include <chrono>

namespace KongLie3D
{
    inline float KongLieSfxVolume = 0.6f;

    inline const char* U8Text(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    inline void PlayKongLieSfx(const std::string& soundPath, float volumeScale = 1.0f, uint64_t minIntervalMs = 50)
    {
        static std::unordered_map<std::string, uint64_t> lastPlayMsBySound;

        using namespace std::chrono;
        const uint64_t nowMs = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        const auto it = lastPlayMsBySound.find(soundPath);
        if (it != lastPlayMsBySound.end() && nowMs - it->second < minIntervalMs)
        {
            return;
        }

        lastPlayMsBySound[soundPath] = nowMs;

        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->PlaySound(soundPath, false, std::clamp(KongLieSfxVolume * volumeScale, 0.0f, 1.0f));
        }
    }

    inline void PlayUiClickSfx()
    {
        PlayKongLieSfx("assets/sounds/konglie/ui_click.wav", 0.55f, 40);
    }

    inline void PlayAttackHitSfx(const std::string& attackType)
    {
        PlayKongLieSfx(attackType == "ap" ? "assets/sounds/konglie/attack_hit_ap.wav"
                                          : "assets/sounds/konglie/attack_hit_ad.wav",
                       0.70f,
                       50);
    }

    inline void PlaySkillCastSfx()
    {
        PlayKongLieSfx("assets/sounds/konglie/skill_cast.wav", 0.70f, 60);
    }

    inline void PlayUnitDieSfx()
    {
        PlayKongLieSfx("assets/sounds/konglie/unit_die.wav", 0.80f, 45);
    }

    inline void PlayBattleStartSfx()
    {
        PlayKongLieSfx("assets/sounds/konglie/battle_start.wav", 0.75f, 0);
    }

    inline void PlayOutcomeSfx(const std::string& winnerTeam)
    {
        if (winnerTeam == "player")
        {
            PlayKongLieSfx("assets/sounds/konglie/victory.wav", 0.85f, 0);
        }
        else if (winnerTeam == "enemy")
        {
            PlayKongLieSfx("assets/sounds/konglie/defeat.wav", 0.85f, 0);
        }
    }
}
