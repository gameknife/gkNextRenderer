#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextLocalization.h"
#include "Runtime/Subsystems/NextAudio.h"

namespace KongLie3D
{
    inline float KongLieSfxVolume = 0.6f;

    inline const char* U8Text(const char8_t* text)
    {
        static thread_local std::string localized;
        const char* fallback = reinterpret_cast<const char*>(text);
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextLocalization* localization = engine->GetLocalization())
            {
                localized = localization->Get(fallback, fallback);
                return localized.c_str();
            }
        }
        return fallback;
    }

    inline void PlayKongLieSfx(const std::string& soundPath, float volumeScale = 1.0f, uint64_t minIntervalMs = 50)
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextAudio* audio = engine->GetAudio())
            {
                audio->PlaySfx(soundPath, std::clamp(KongLieSfxVolume * volumeScale, 0.0f, 1.0f), minIntervalMs);
            }
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
