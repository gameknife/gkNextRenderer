#pragma once

#include "Common/CoreMinimal.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if WITH_AUDIO
struct ma_device;
#endif
#if WITH_AUDIO && WITH_WHISPERCPP
struct whisper_context;
#endif

namespace NextAI
{
    struct FVoiceInputConfig
    {
        bool enabled = false;
        std::string model = "base";
        std::string language = "zh";
        int threads = 4;
        int maxRecordSeconds = 20;
        int sampleRate = 16000;
        bool autoSend = false;
        bool keepTempFiles = false;
    };

    struct FVoiceTranscriptionResult
    {
        bool success = false;
        std::string text;
        std::string message;

        static FVoiceTranscriptionResult Success(std::string textIn)
        {
            FVoiceTranscriptionResult result;
            result.success = true;
            result.text = std::move(textIn);
            return result;
        }

        static FVoiceTranscriptionResult Failure(std::string messageIn)
        {
            FVoiceTranscriptionResult result;
            result.success = false;
            result.message = std::move(messageIn);
            return result;
        }
    };

    class VoiceInputService final
    {
    public:
        VoiceInputService();
        ~VoiceInputService();

        bool Initialize(const FVoiceInputConfig& config);

        bool BeginCapture();
        void EndCaptureAndTranscribeAsync();

        bool HasPendingResult() const { return hasPendingResult_.load(); }
        FVoiceTranscriptionResult ConsumePendingResult();

        bool IsEnabled() const { return config_.enabled; }
        bool IsCapturing() const { return capturing_.load(); }
        bool IsTranscribing() const { return transcribing_.load(); }

        std::string GetStatusMessage() const;

    private:
        void SetStatusMessage(std::string message);
        FVoiceTranscriptionResult MakeFailureResult(const std::string& message);

#if WITH_AUDIO
        static void CaptureCallback(ma_device* device, void* output, const void* input, uint32_t frameCount);
        void AppendCapturedSamples(const int16_t* input, uint32_t sampleCount);

        FVoiceTranscriptionResult TranscribeSamples(const std::vector<int16_t>& samples);
        bool StartCaptureDevice();
        void StopCaptureDevice();
#endif

#if WITH_AUDIO && WITH_WHISPERCPP
        bool EnsureWhisperContextLoaded(const std::filesystem::path& modelPath);
#endif

        FVoiceInputConfig config_{};

        std::atomic<bool> hasPendingResult_{false};
        std::atomic<bool> capturing_{false};
        std::atomic<bool> transcribing_{false};

        mutable std::mutex stateMutex_;
        std::mutex resultMutex_;
        FVoiceTranscriptionResult pendingResult_{};
        std::string statusMessage_ = "Voice input not initialized";

#if WITH_AUDIO
        ma_device* captureDevice_ = nullptr;
        std::mutex captureMutex_;
        std::vector<int16_t> capturedSamples_;
#endif

#if WITH_AUDIO && WITH_WHISPERCPP
        std::mutex whisperMutex_;
        whisper_context* whisperContext_ = nullptr;
        std::string loadedModelPath_;
#endif

        std::thread transcribeThread_;
    };
} // namespace NextAI
