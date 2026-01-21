#include "NextAudio.h"

#include "Utilities/FileHelper.hpp"

#if WITH_AUDIO
#define MINIAUDIO_IMPLEMENTATION
#include "ThirdParty/miniaudio/miniaudio.h"
#endif

NextAudio::NextAudio() = default;

NextAudio::~NextAudio()
{
    Stop();
}

void NextAudio::Start()
{
#if WITH_AUDIO
    if (audioEngine_)
    {
        return;
    }

    audioEngine_ = std::make_unique<ma_engine>();

    ma_result result = ma_engine_init(nullptr, audioEngine_.get());
    if (result != MA_SUCCESS)
    {
        audioEngine_.reset();
    }
#endif
}

void NextAudio::Stop()
{
#if WITH_AUDIO
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
#endif
}

void NextAudio::PlaySound(const std::string& soundName, bool loop, float volume)
{
#if WITH_AUDIO
    if (!audioEngine_)
    {
        return;
    }

    if (soundMaps_.find(soundName) == soundMaps_.end())
    {
        auto sound = std::make_unique<ma_sound>();

        soundDataMaps_[soundName] = std::vector<uint8_t>();

        if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(soundName, soundDataMaps_[soundName]))
        {
            soundDecoderMaps_[soundName] = std::make_unique<ma_decoder>();
            ma_decoder_init_memory(soundDataMaps_[soundName].data(), soundDataMaps_[soundName].size(), nullptr, soundDecoderMaps_[soundName].get());
            ma_sound_init_from_data_source(audioEngine_.get(), soundDecoderMaps_[soundName].get(), 0, nullptr, sound.get());
        }

        soundMaps_[soundName] = std::move(sound);
    }

    ma_sound* sound = soundMaps_[soundName].get();

    ma_sound_stop(sound);
    ma_sound_set_looping(sound, loop);
    ma_sound_set_volume(sound, volume);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
#endif
}

void NextAudio::PauseSound(const std::string& soundName, bool pause)
{
#if WITH_AUDIO
    if (soundMaps_.find(soundName) == soundMaps_.end())
    {
        return;
    }

    ma_sound* sound = soundMaps_[soundName].get();
    pause ? ma_sound_stop(sound) : ma_sound_start(sound);
#endif
}

bool NextAudio::IsSoundPlaying(const std::string& soundName)
{
#if WITH_AUDIO
    if (soundMaps_.find(soundName) == soundMaps_.end())
    {
        return false;
    }
    ma_sound* sound = soundMaps_[soundName].get();
    return ma_sound_is_playing(sound);
#else
    return false;
#endif
}
