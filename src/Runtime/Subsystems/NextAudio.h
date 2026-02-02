#pragma once

#include "Common/CoreMinimal.hpp"
#include "Vulkan/Vulkan.hpp"

#if WITH_AUDIO
struct ma_engine;
struct ma_sound;
struct ma_decoder;
#endif

class NextAudio final
{
public:
    VULKAN_NON_COPIABLE(NextAudio)

    NextAudio();
    ~NextAudio();

    void Start();
    void Stop();

    void PlaySound(const std::string& soundName, bool loop = false, float volume = 1.0f);
    void PauseSound(const std::string& soundName, bool pause);
    bool IsSoundPlaying(const std::string& soundName);

private:
#if WITH_AUDIO
    std::unique_ptr<ma_engine> audioEngine_;
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> soundMaps_;
    std::unordered_map<std::string, std::vector<uint8_t>> soundDataMaps_;
    std::unordered_map<std::string, std::unique_ptr<ma_decoder>> soundDecoderMaps_;
#endif
};
