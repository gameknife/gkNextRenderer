#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAudio/MiniaudioBackend.hpp"

#include "Engine/Utilities/FileHelper.hpp"

#include <chrono>
#include <random>

#define MINIAUDIO_IMPLEMENTATION
#include "ThirdParty/miniaudio/miniaudio.h"

namespace Modules::Audio
{
    FMiniaudioBackend::~FMiniaudioBackend()
    {
        Stop();
    }

    void FMiniaudioBackend::Start()
    {
        if (audioEngine_)
        {
            return;
        }

        audioEngine_ = std::make_unique<ma_engine>();
        if (ma_engine_init(nullptr, audioEngine_.get()) != MA_SUCCESS)
        {
            audioEngine_.reset();
        }
    }

    void FMiniaudioBackend::Stop()
    {
        soundDataMaps_.clear();
        for (auto& [name, sound] : soundMaps_)
        {
            ma_sound_uninit(sound.get());
        }
        for (auto& [name, decoder] : soundDecoderMaps_)
        {
            ma_decoder_uninit(decoder.get());
        }
        soundMaps_.clear();
        soundDecoderMaps_.clear();

        if (audioEngine_)
        {
            ma_engine_uninit(audioEngine_.get());
            audioEngine_.reset();
        }
    }

    void FMiniaudioBackend::PlaySound(const std::string& soundName, bool loop, float volume)
    {
        if (!audioEngine_)
        {
            return;
        }

        if (!soundMaps_.contains(soundName))
        {
            auto sound = std::make_unique<ma_sound>();
            soundDataMaps_[soundName] = {};

            if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(soundName, soundDataMaps_[soundName]))
            {
                soundDecoderMaps_[soundName] = std::make_unique<ma_decoder>();
                ma_decoder_init_memory(soundDataMaps_[soundName].data(), soundDataMaps_[soundName].size(), nullptr,
                                       soundDecoderMaps_[soundName].get());
                ma_sound_init_from_data_source(audioEngine_.get(), soundDecoderMaps_[soundName].get(), 0, nullptr,
                                               sound.get());
            }

            soundMaps_[soundName] = std::move(sound);
        }

        ma_sound* sound = soundMaps_[soundName].get();
        ma_sound_stop(sound);
        ma_sound_set_looping(sound, loop);
        ma_sound_set_volume(sound, volume);
        ma_sound_seek_to_pcm_frame(sound, 0);
        ma_sound_start(sound);
    }

    bool FMiniaudioBackend::IsSoundAssetAvailable(const std::string& path)
    {
        return Utilities::FileHelper::IsAssetAvailable(path);
    }

    void FMiniaudioBackend::PlaySfx(const std::string& path, float volume, uint64_t minIntervalMs)
    {
        if (path.empty())
        {
            return;
        }

        using namespace std::chrono;
        const uint64_t nowMs = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        const auto lastPlayIt = lastPlayMsBySound_.find(path);
        if (lastPlayIt != lastPlayMsBySound_.end() && nowMs - lastPlayIt->second < minIntervalMs)
        {
            return;
        }
        lastPlayMsBySound_[path] = nowMs;

        if (!IsSoundAssetAvailable(path))
        {
            if (missingSounds_.insert(path).second)
            {
                spdlog::warn("[NextAudio] missing sound '{}'", path);
            }
            return;
        }

        PlaySound(path, false, std::clamp(volume, 0.0f, 1.0f));
    }

    void FMiniaudioBackend::PlaySfxVariant(std::initializer_list<std::string_view> candidates,
                                           float volume,
                                           uint64_t minIntervalMs)
    {
        if (candidates.size() == 0)
        {
            return;
        }

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        auto iter = candidates.begin();
        std::advance(iter, static_cast<std::ptrdiff_t>(dist(rng)));
        PlaySfx(std::string(*iter), volume, minIntervalMs);
    }

    void FMiniaudioBackend::PlayMusic(const std::string& path, float volume)
    {
        if (path.empty())
        {
            return;
        }
        if (currentMusicPath_ == path)
        {
            SetMusicVolume(volume);
            return;
        }

        StopMusic();
        if (!IsSoundAssetAvailable(path))
        {
            if (missingSounds_.insert(path).second)
            {
                spdlog::warn("[NextAudio] missing music '{}'", path);
            }
            return;
        }

        currentMusicPath_ = path;
        musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
        PlaySound(currentMusicPath_, true, musicVolume_);
    }

    void FMiniaudioBackend::StopMusic()
    {
        if (!currentMusicPath_.empty())
        {
            PauseSound(currentMusicPath_, true);
            currentMusicPath_.clear();
        }
    }

    void FMiniaudioBackend::SetMusicVolume(float volume)
    {
        musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
        if (!currentMusicPath_.empty())
        {
            const std::string path = currentMusicPath_;
            PauseSound(path, true);
            PlaySound(path, true, musicVolume_);
        }
    }

    void FMiniaudioBackend::PauseSound(const std::string& soundName, bool pause)
    {
        const auto sound = soundMaps_.find(soundName);
        if (sound != soundMaps_.end())
        {
            pause ? ma_sound_stop(sound->second.get()) : ma_sound_start(sound->second.get());
        }
    }

    bool FMiniaudioBackend::IsSoundPlaying(const std::string& soundName)
    {
        const auto sound = soundMaps_.find(soundName);
        return sound != soundMaps_.end() && ma_sound_is_playing(sound->second.get());
    }
}
