#include "Runtime/Subsystems/VoiceInputService.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <spdlog/spdlog.h>

#if WITH_AUDIO
#include "ThirdParty/miniaudio/miniaudio.h"
#endif

#if WITH_AUDIO && WITH_WHISPERCPP
#include "Utilities/FileHelper.hpp"
#include <curl/curl.h>
#include <whisper.h>
#endif

namespace NextAI
{
    namespace
    {
        std::string TrimString(const std::string& input)
        {
            const size_t start = input.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
            {
                return "";
            }

            const size_t end = input.find_last_not_of(" \t\r\n");
            return input.substr(start, end - start + 1);
        }

#if WITH_AUDIO && WITH_WHISPERCPP
        std::string NormalizeModelChoice(const std::string& model)
        {
            std::string lower = model;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lower == "tiny" || lower == "base" || lower == "small")
            {
                return lower;
            }
            return "";
        }

        std::filesystem::path ResolveManagedModelPath(const std::string& modelChoice)
        {
            std::filesystem::path whisperDir(Utilities::FileHelper::GetPlatformFilePath("assets/whisper"));
            return whisperDir / fmt::format("ggml-{}.bin", modelChoice);
        }

        std::vector<std::string> BuildModelUrls(const std::string& modelChoice)
        {
            const std::string fileName = fmt::format("ggml-{}.bin", modelChoice);
            return {
                fmt::format("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/{}", fileName),
                fmt::format("https://huggingface.co/datasets/ggerganov/whisper.cpp/resolve/main/{}", fileName)
            };
        }

        size_t CurlWriteFileCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
        {
            auto* file = static_cast<FILE*>(userdata);
            return std::fwrite(ptr, size, nmemb, file);
        }

        bool DownloadFileWithCurl(const std::string& url, const std::filesystem::path& dstPath, std::string& errorOut)
        {
            const std::filesystem::path tmpPath = dstPath.string() + ".part";
            FILE* fp = std::fopen(tmpPath.string().c_str(), "wb");
            if (fp == nullptr)
            {
                errorOut = fmt::format("failed to open temp file: {}", tmpPath.string());
                return false;
            }

            CURL* curl = curl_easy_init();
            if (curl == nullptr)
            {
                std::fclose(fp);
                std::filesystem::remove(tmpPath);
                errorOut = "failed to initialize CURL";
                return false;
            }

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteFileCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

            const CURLcode res = curl_easy_perform(curl);
            long responseCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

            curl_easy_cleanup(curl);
            std::fclose(fp);

            if (res != CURLE_OK || responseCode >= 400)
            {
                std::filesystem::remove(tmpPath);
                errorOut = fmt::format("download failed: {} (http {})", curl_easy_strerror(res), responseCode);
                return false;
            }

            std::error_code ec;
            std::filesystem::rename(tmpPath, dstPath, ec);
            if (ec)
            {
                std::filesystem::remove(tmpPath);
                errorOut = fmt::format("rename temp model failed: {}", ec.message());
                return false;
            }

            return true;
        }
#endif
    } // namespace

    VoiceInputService::VoiceInputService() = default;

    VoiceInputService::~VoiceInputService()
    {
#if WITH_AUDIO
        StopCaptureDevice();
#endif

        if (transcribeThread_.joinable())
        {
            transcribeThread_.join();
        }

#if WITH_AUDIO && WITH_WHISPERCPP
        if (whisperContext_ != nullptr)
        {
            whisper_free(whisperContext_);
            whisperContext_ = nullptr;
        }
#endif
    }

    bool VoiceInputService::Initialize(const FVoiceInputConfig& config)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        config_ = config;

        if (!config_.enabled)
        {
            statusMessage_ = "Voice input disabled";
            return true;
        }

#if !WITH_AUDIO
        statusMessage_ = "Voice input unavailable: WITH_AUDIO is OFF";
        return false;
#elif !WITH_WHISPERCPP
        statusMessage_ = "Voice input unavailable: WITH_WHISPERCPP is OFF";
        return false;
#else
        statusMessage_ = "Voice input ready";
        return true;
#endif
    }

    bool VoiceInputService::BeginCapture()
    {
        if (!config_.enabled)
        {
            SetStatusMessage("Voice input disabled");
            return false;
        }

#if WITH_AUDIO && WITH_WHISPERCPP
        if (transcribing_.load())
        {
            SetStatusMessage("Voice input busy: transcribing");
            return false;
        }

        if (capturing_.load())
        {
            return true;
        }

        if (transcribeThread_.joinable())
        {
            transcribeThread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(captureMutex_);
            capturedSamples_.clear();
            capturedSamples_.reserve(static_cast<size_t>(config_.sampleRate) * static_cast<size_t>(config_.maxRecordSeconds));
        }

        if (!StartCaptureDevice())
        {
            return false;
        }

        capturing_ = true;
        SetStatusMessage("Recording... release to transcribe");
        return true;
#else
        SetStatusMessage("Voice input unavailable in current build");
        return false;
#endif
    }

    void VoiceInputService::EndCaptureAndTranscribeAsync()
    {
#if WITH_AUDIO && WITH_WHISPERCPP
        if (!capturing_.load())
        {
            return;
        }

        StopCaptureDevice();
        capturing_ = false;

        std::vector<int16_t> samples;
        {
            std::lock_guard<std::mutex> lock(captureMutex_);
            samples = capturedSamples_;
        }

        if (samples.empty())
        {
            MakeFailureResult("No audio captured");
            return;
        }

        const size_t minSampleCount = static_cast<size_t>(std::max(1, config_.sampleRate / 3));
        if (samples.size() < minSampleCount)
        {
            MakeFailureResult("Recording too short");
            return;
        }

        transcribing_ = true;
        SetStatusMessage("Transcribing...");

        transcribeThread_ = std::thread([this, samples = std::move(samples)]() {
            auto result = TranscribeSamples(samples);
            const bool success = result.success;
            const std::string message = success ? std::string("Voice input ready") : result.message;
            {
                std::lock_guard<std::mutex> lock(resultMutex_);
                pendingResult_ = std::move(result);
                hasPendingResult_ = true;
            }

            SetStatusMessage(message);
            transcribing_ = false;
        });
#endif
    }

    FVoiceTranscriptionResult VoiceInputService::ConsumePendingResult()
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        hasPendingResult_ = false;
        return pendingResult_;
    }

    std::string VoiceInputService::GetStatusMessage() const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return statusMessage_;
    }

    void VoiceInputService::SetStatusMessage(std::string message)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        statusMessage_ = std::move(message);
    }

    FVoiceTranscriptionResult VoiceInputService::MakeFailureResult(const std::string& message)
    {
        FVoiceTranscriptionResult result = FVoiceTranscriptionResult::Failure(message);
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            pendingResult_ = result;
            hasPendingResult_ = true;
        }
        SetStatusMessage(message);
        return result;
    }

#if WITH_AUDIO
    void VoiceInputService::CaptureCallback(ma_device* device, void* output, const void* input, uint32_t frameCount)
    {
        (void)output;

        auto* self = static_cast<VoiceInputService*>(device->pUserData);
        if (!self || input == nullptr || frameCount == 0)
        {
            return;
        }

        const auto* pcm = static_cast<const int16_t*>(input);
        self->AppendCapturedSamples(pcm, frameCount);
    }

    void VoiceInputService::AppendCapturedSamples(const int16_t* input, uint32_t sampleCount)
    {
        if (input == nullptr || sampleCount == 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(captureMutex_);
        const size_t maxSamples = static_cast<size_t>(config_.sampleRate) * static_cast<size_t>(config_.maxRecordSeconds);
        if (capturedSamples_.size() >= maxSamples)
        {
            return;
        }

        const size_t writable = std::min<size_t>(sampleCount, maxSamples - capturedSamples_.size());
        capturedSamples_.insert(capturedSamples_.end(), input, input + writable);
    }

    bool VoiceInputService::StartCaptureDevice()
    {
        if (captureDevice_ != nullptr)
        {
            StopCaptureDevice();
        }

        auto* device = new ma_device();
        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_s16;
        config.capture.channels = 1;
        config.sampleRate = static_cast<ma_uint32>(config_.sampleRate);
        config.dataCallback = &VoiceInputService::CaptureCallback;
        config.pUserData = this;

        const ma_result initResult = ma_device_init(nullptr, &config, device);
        if (initResult != MA_SUCCESS)
        {
            delete device;
            SetStatusMessage(fmt::format("Failed to init capture device: {}", static_cast<int>(initResult)));
            return false;
        }

        const ma_result startResult = ma_device_start(device);
        if (startResult != MA_SUCCESS)
        {
            ma_device_uninit(device);
            delete device;
            SetStatusMessage(fmt::format("Failed to start capture device: {}", static_cast<int>(startResult)));
            return false;
        }

        captureDevice_ = device;
        return true;
    }

    void VoiceInputService::StopCaptureDevice()
    {
        if (captureDevice_ == nullptr)
        {
            return;
        }

        ma_device_uninit(captureDevice_);
        delete captureDevice_;
        captureDevice_ = nullptr;
    }
#endif

#if WITH_AUDIO && WITH_WHISPERCPP
    bool VoiceInputService::EnsureWhisperContextLoaded(const std::filesystem::path& modelPath)
    {
        std::lock_guard<std::mutex> lock(whisperMutex_);

        const std::string modelPathStr = modelPath.string();
        if (whisperContext_ != nullptr && modelPathStr == loadedModelPath_)
        {
            return true;
        }

        if (whisperContext_ != nullptr)
        {
            whisper_free(whisperContext_);
            whisperContext_ = nullptr;
            loadedModelPath_.clear();
        }

        whisper_context_params contextParams = whisper_context_default_params();
        whisperContext_ = whisper_init_from_file_with_params(modelPathStr.c_str(), contextParams);
        if (whisperContext_ == nullptr)
        {
            return false;
        }

        loadedModelPath_ = modelPathStr;
        return true;
    }

    FVoiceTranscriptionResult VoiceInputService::TranscribeSamples(const std::vector<int16_t>& samples)
    {
        const std::string modelChoice = NormalizeModelChoice(config_.model);
        if (modelChoice.empty())
        {
            return FVoiceTranscriptionResult::Failure("invalid voiceInput.model, expected tiny/base/small");
        }

        const std::filesystem::path modelPath = ResolveManagedModelPath(modelChoice);
        std::error_code ec;
        if (!std::filesystem::exists(modelPath, ec))
        {
            std::filesystem::create_directories(modelPath.parent_path(), ec);
            if (ec)
            {
                return FVoiceTranscriptionResult::Failure(
                    fmt::format("failed to create whisper model directory: {}", ec.message()));
            }

            SetStatusMessage(fmt::format("Downloading whisper {} model...", modelChoice));
            bool downloaded = false;
            std::string lastError;
            for (const auto& url : BuildModelUrls(modelChoice))
            {
                if (DownloadFileWithCurl(url, modelPath, lastError))
                {
                    downloaded = true;
                    break;
                }
                SPDLOG_WARN("[VoiceInput] model download failed from {}: {}", url, lastError);
            }

            if (!downloaded)
            {
                return FVoiceTranscriptionResult::Failure(
                    fmt::format("failed to download whisper {} model: {}", modelChoice, lastError));
            }
        }

        if (!EnsureWhisperContextLoaded(modelPath))
        {
            return FVoiceTranscriptionResult::Failure("failed to initialize whisper context");
        }

        std::vector<float> pcmf32;
        pcmf32.resize(samples.size());
        constexpr float kInvPcm16 = 1.0f / 32768.0f;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            pcmf32[i] = static_cast<float>(samples[i]) * kInvPcm16;
        }

        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.print_realtime = false;
        params.print_progress = false;
        params.print_timestamps = false;
        params.print_special = false;
        params.no_context = true;
        params.n_threads = std::max(1, config_.threads);
        const std::string language = config_.language.empty() ? "zh" : config_.language;
        params.language = language.c_str();

        int fullResult = -1;
        std::string allText;
        {
            std::lock_guard<std::mutex> lock(whisperMutex_);

            fullResult = whisper_full(whisperContext_, params, pcmf32.data(), static_cast<int>(pcmf32.size()));
            if (fullResult == 0)
            {
                const int segmentCount = whisper_full_n_segments(whisperContext_);
                for (int i = 0; i < segmentCount; ++i)
                {
                    const char* segmentText = whisper_full_get_segment_text(whisperContext_, i);
                    if (segmentText != nullptr)
                    {
                        allText += segmentText;
                    }
                }
            }
        }

        if (fullResult != 0)
        {
            return FVoiceTranscriptionResult::Failure(fmt::format("whisper inference failed: {}", fullResult));
        }

        const std::string trimmed = TrimString(allText);
        if (trimmed.empty())
        {
            return FVoiceTranscriptionResult::Failure("whisper output is empty");
        }

        return FVoiceTranscriptionResult::Success(trimmed);
    }
#elif WITH_AUDIO
    FVoiceTranscriptionResult VoiceInputService::TranscribeSamples(const std::vector<int16_t>& samples)
    {
        (void)samples;
        return FVoiceTranscriptionResult::Failure("whisper.cpp is not enabled");
    }
#endif

} // namespace NextAI
