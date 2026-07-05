#pragma once

#include "Engine/Common/CoreMinimal.hpp"

struct ma_engine;
struct ma_sound;
struct ma_decoder;

class NextAudio final
{
public:
    GK_NON_COPIABLE(NextAudio)

    NextAudio();
    ~NextAudio();

    void Start();
    void Stop();

    void PlaySound(const std::string& soundName, bool loop = false, float volume = 1.0f);
    void PauseSound(const std::string& soundName, bool pause);
    bool IsSoundPlaying(const std::string& soundName);
    void PlaySfx(const std::string& path, float volume = 1.0f, uint64_t minIntervalMs = 50);
    void PlaySfxVariant(std::initializer_list<std::string_view> candidates, float volume = 1.0f, uint64_t minIntervalMs = 50);
    void PlayMusic(const std::string& path, float volume = 1.0f);
    void StopMusic();
    void SetMusicVolume(float volume);
    const std::string& GetCurrentMusicPath() const { return currentMusicPath_; }

private:
    bool IsSoundAssetAvailable(const std::string& path);

    std::unordered_map<std::string, uint64_t> lastPlayMsBySound_;
    std::unordered_set<std::string> missingSounds_;
    std::string currentMusicPath_;
    float musicVolume_ = 1.0f;
    std::unique_ptr<ma_engine> audioEngine_;
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> soundMaps_;
    std::unordered_map<std::string, std::vector<uint8_t>> soundDataMaps_;
    std::unordered_map<std::string, std::unique_ptr<ma_decoder>> soundDecoderMaps_;
};
