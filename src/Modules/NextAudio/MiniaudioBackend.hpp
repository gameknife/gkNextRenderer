#pragma once

#include "Engine/Runtime/Subsystems/NextAudio.h"

struct ma_decoder;
struct ma_engine;
struct ma_sound;

namespace Modules::Audio
{
    class FMiniaudioBackend final : public NextAudio
    {
    public:
        FMiniaudioBackend() = default;
        ~FMiniaudioBackend() override;

        void Start() override;
        void Stop() override;
        void PlaySound(const std::string& soundName, bool loop, float volume) override;
        void PauseSound(const std::string& soundName, bool pause) override;
        bool IsSoundPlaying(const std::string& soundName) override;
        void PlaySfx(const std::string& path, float volume, uint64_t minIntervalMs) override;
        void PlaySfxVariant(std::initializer_list<std::string_view> candidates,
                            float volume,
                            uint64_t minIntervalMs) override;
        void PlayMusic(const std::string& path, float volume) override;
        void StopMusic() override;
        void SetMusicVolume(float volume) override;
        const std::string& GetCurrentMusicPath() const override { return currentMusicPath_; }

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
}
