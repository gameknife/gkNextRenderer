#pragma once

#include "Engine/Common/CoreMinimal.hpp"

// Core-facing audio service contract. The concrete backend lives in the
// optional Modules/NextAudio library and is installed by the application.
class NextAudio
{
public:
    GK_NON_COPIABLE(NextAudio)

    NextAudio() = default;
    virtual ~NextAudio() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    virtual void PlaySound(const std::string& soundName, bool loop = false, float volume = 1.0f) = 0;
    virtual void PauseSound(const std::string& soundName, bool pause) = 0;
    virtual bool IsSoundPlaying(const std::string& soundName) = 0;
    virtual void PlaySfx(const std::string& path, float volume = 1.0f, uint64_t minIntervalMs = 50) = 0;
    virtual void PlaySfxVariant(std::initializer_list<std::string_view> candidates,
                                float volume = 1.0f,
                                uint64_t minIntervalMs = 50) = 0;
    virtual void PlayMusic(const std::string& path, float volume = 1.0f) = 0;
    virtual void StopMusic() = 0;
    virtual void SetMusicVolume(float volume) = 0;
    virtual const std::string& GetCurrentMusicPath() const = 0;
};
