#include "Engine/Runtime/ScreenShotService.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/ScreenShot.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/StbImage.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <system_error>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <stb_image_resize2.h>
#include <webp/encode.h>
#include <webp/mux.h>

namespace Runtime
{
    struct FVideoCaptureState final
    {
        FScreenShotService::EAnimationFormat format = FScreenShotService::EAnimationFormat::Both;
        uint32_t framesPerSecond = 15;
        uint32_t frameCount = 45;
        uint32_t nextFrameIndex = 0;
        double elapsedSeconds = 0.0;
        double nextFrameTime = 0.0;
        bool frameInFlight = false;
        FScreenShotService::EVideoOutputScale outputScale = FScreenShotService::EVideoOutputScale::Half;
        bool includeUi = false;
        bool forceUiHidden = false;
        std::filesystem::path temporaryDirectory;
        std::string outputStem;
        std::string outputPath;
        std::future<bool> encodingFuture;
        std::function<void()> onCaptureFinished;
        std::function<void(const std::string& path)> onCompleted;
    };

    namespace
    {
        constexpr double kThreeSecondVideoDuration = 3.0;
        constexpr uint32_t kMaximumVideoFramesPerSecond = 120;

        struct FVideoDimensions final
        {
            int width = 0;
            int height = 0;
        };

        std::string SanitizeTag(const std::string& tag)
        {
            std::string sanitized;
            sanitized.reserve(tag.size());
            for (const unsigned char character : tag)
            {
                if (std::isalnum(character) != 0 || character == '-' || character == '_')
                {
                    sanitized.push_back(static_cast<char>(character));
                }
                else
                {
                    sanitized.push_back('-');
                }
            }
            return sanitized;
        }

        uint32_t DefaultFramesPerSecond(const FScreenShotService::EAnimationFormat format)
        {
            return format == FScreenShotService::EAnimationFormat::Gif ? 15u : 30u;
        }

        const char* AnimationExtension(const FScreenShotService::EAnimationFormat format)
        {
            return format == FScreenShotService::EAnimationFormat::AnimatedWebp ? ".webp" : ".gif";
        }

        std::string QuoteProcessArgument(const std::filesystem::path& path)
        {
            const std::string value = path.string();
            std::string quoted;
            quoted.reserve(value.size() + 2);
            quoted.push_back('"');
            for (const char character : value)
            {
                if (character == '"')
                {
                    quoted += "\\\"";
                }
                else
                {
                    quoted.push_back(character);
                }
            }
            quoted.push_back('"');
            return quoted;
        }

        std::filesystem::path ResolveFfmpegPath()
        {
#if WIN32
            constexpr const char* ffmpegName = "ffmpeg.exe";
#else
            constexpr const char* ffmpegName = "ffmpeg";
#endif
            const std::filesystem::path executablePath = NextRenderer::GetExecutableDirectory() / ffmpegName;
            std::error_code errorCode;
            if (std::filesystem::is_regular_file(executablePath, errorCode))
            {
                return executablePath;
            }
            return {};
        }

        double GetVideoOutputScale(const FScreenShotService::EVideoOutputScale outputScale)
        {
            switch (outputScale)
            {
            case FScreenShotService::EVideoOutputScale::Half:
                return 0.5;
            case FScreenShotService::EVideoOutputScale::Quarter:
                return 0.25;
            case FScreenShotService::EVideoOutputScale::Full:
            default:
                return 1.0;
            }
        }

        bool ResolveVideoDimensions(const FVideoCaptureState& capture,
                                    FVideoDimensions& outDimensions)
        {
            const std::filesystem::path firstFramePath = capture.temporaryDirectory / "frame_000000.jpg";
            int inputWidth = 0;
            int inputHeight = 0;
            if (!stbi_info(firstFramePath.string().c_str(), &inputWidth, &inputHeight, nullptr))
            {
                spdlog::error("Failed to inspect temporary video frame {}", firstFramePath.string());
                return false;
            }

            const double scale = GetVideoOutputScale(capture.outputScale);
            outDimensions.width = std::max(
                1, static_cast<int>(std::floor(static_cast<double>(inputWidth) * scale)));
            outDimensions.height = std::max(
                1, static_cast<int>(std::floor(static_cast<double>(inputHeight) * scale)));
            return true;
        }

        std::string BuildFfmpegCommand(const FVideoCaptureState& capture,
                                       const std::filesystem::path& ffmpegPath,
                                       const FVideoDimensions dimensions,
                                       const std::string& outputPath)
        {
            const std::filesystem::path inputPattern = capture.temporaryDirectory / "frame_%06d.jpg";
            const std::string commonArguments = QuoteProcessArgument(ffmpegPath) +
                " -hide_banner -loglevel error -y -framerate " + std::to_string(capture.framesPerSecond) +
                " -i " + QuoteProcessArgument(inputPattern) +
                " -frames:v " + std::to_string(capture.frameCount) +
                " -t " + fmt::format("{:.3f}", kThreeSecondVideoDuration);

            return commonArguments +
                " -vf \"scale=" + std::to_string(dimensions.width) + ":" +
                std::to_string(dimensions.height) +
                ",split[s0][s1];[s0]palettegen=stats_mode=diff[p];[s1][p]paletteuse=dither=sierra2_4a\""
                " -loop -1 -an " + QuoteProcessArgument(outputPath);
        }

        bool EncodeAnimatedWebpWithLibwebp(const FVideoCaptureState& capture,
                                           const FVideoDimensions dimensions,
                                           const std::string& outputPath)
        {
            WebPAnimEncoderOptions encoderOptions{};
            if (!WebPAnimEncoderOptionsInit(&encoderOptions))
            {
                spdlog::error("Failed to initialize animated WebP encoder options");
                return false;
            }
            encoderOptions.anim_params.loop_count = 0;

            WebPConfig config{};
            if (!WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, 90.0f) ||
                !WebPValidateConfig(&config))
            {
                spdlog::error("Failed to initialize animated WebP encoder config");
                return false;
            }
            config.method = 4;

            WebPAnimEncoder* encoder = nullptr;
            int inputWidth = 0;
            int inputHeight = 0;
            bool succeeded = true;
            for (uint32_t frameIndex = 0; frameIndex < capture.frameCount; ++frameIndex)
            {
                const std::filesystem::path framePath = capture.temporaryDirectory /
                    fmt::format("frame_{:06}.jpg", frameIndex);
                int frameWidth = 0;
                int frameHeight = 0;
                stbi_uc* pixels = stbi_load(framePath.string().c_str(), &frameWidth, &frameHeight,
                                            nullptr, 3);
                if (!pixels)
                {
                    spdlog::error("Failed to load temporary video frame {}", framePath.string());
                    succeeded = false;
                    break;
                }

                if (!encoder)
                {
                    inputWidth = frameWidth;
                    inputHeight = frameHeight;
                    encoder = WebPAnimEncoderNew(dimensions.width, dimensions.height, &encoderOptions);
                    if (!encoder)
                    {
                        spdlog::error("Failed to create animated WebP encoder");
                        stbi_image_free(pixels);
                        succeeded = false;
                        break;
                    }
                }

                if (frameWidth != inputWidth || frameHeight != inputHeight)
                {
                    spdlog::error("Temporary video frame {} has inconsistent dimensions", framePath.string());
                    stbi_image_free(pixels);
                    succeeded = false;
                    break;
                }

                std::vector<uint8_t> resizedPixels;
                const stbi_uc* framePixels = pixels;
                int frameStride = frameWidth * 3;
                if (frameWidth != dimensions.width || frameHeight != dimensions.height)
                {
                    resizedPixels.resize(static_cast<size_t>(dimensions.width) * dimensions.height * 3);
                    if (!stbir_resize_uint8_srgb(
                            pixels, frameWidth, frameHeight, frameStride,
                            resizedPixels.data(), dimensions.width, dimensions.height,
                            dimensions.width * 3, STBIR_RGB))
                    {
                        spdlog::error("Failed to resize temporary video frame {}", framePath.string());
                        stbi_image_free(pixels);
                        succeeded = false;
                        break;
                    }
                    framePixels = resizedPixels.data();
                    frameWidth = dimensions.width;
                    frameHeight = dimensions.height;
                    frameStride = dimensions.width * 3;
                }

                WebPPicture picture{};
                const bool pictureInitialized = WebPPictureInit(&picture);
                picture.width = frameWidth;
                picture.height = frameHeight;
                if (!pictureInitialized ||
                    !WebPPictureImportRGB(&picture, framePixels, frameStride))
                {
                    spdlog::error("Failed to prepare animated WebP frame {}", framePath.string());
                    WebPPictureFree(&picture);
                    stbi_image_free(pixels);
                    succeeded = false;
                    break;
                }

                const int timestampMilliseconds = static_cast<int>(
                    (static_cast<uint64_t>(frameIndex) * 1000u) / capture.framesPerSecond);
                if (!WebPAnimEncoderAdd(encoder, &picture, timestampMilliseconds, &config))
                {
                    const char* error = WebPAnimEncoderGetError(encoder);
                    spdlog::error("Failed to encode animated WebP frame {}: {}", framePath.string(),
                                  error ? error : "unknown error");
                    succeeded = false;
                }

                WebPPictureFree(&picture);
                stbi_image_free(pixels);
                if (!succeeded)
                {
                    break;
                }
            }

            if (succeeded && encoder)
            {
                succeeded = WebPAnimEncoderAdd(encoder, nullptr,
                                               static_cast<int>(kThreeSecondVideoDuration * 1000.0),
                                               nullptr) != 0;
                if (!succeeded)
                {
                    const char* error = WebPAnimEncoderGetError(encoder);
                    spdlog::error("Failed to finalize animated WebP: {}", error ? error : "unknown error");
                }
            }

            WebPData outputData{};
            if (succeeded && encoder && !WebPAnimEncoderAssemble(encoder, &outputData))
            {
                const char* error = WebPAnimEncoderGetError(encoder);
                spdlog::error("Failed to assemble animated WebP: {}", error ? error : "unknown error");
                succeeded = false;
            }

            if (succeeded && outputData.bytes && outputData.size > 0)
            {
                std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
                if (output.is_open())
                {
                    output.write(reinterpret_cast<const char*>(outputData.bytes),
                                 static_cast<std::streamsize>(outputData.size));
                    succeeded = output.good();
                }
                else
                {
                    succeeded = false;
                }
                if (!succeeded)
                {
                    spdlog::error("Failed to write animated WebP {}", outputPath);
                }
            }

            WebPDataClear(&outputData);
            if (encoder)
            {
                WebPAnimEncoderDelete(encoder);
            }
            return succeeded;
        }

        std::string BuildAnimationPath(const FVideoCaptureState& capture,
                                       const FScreenShotService::EAnimationFormat format)
        {
            return capture.outputStem + AnimationExtension(format);
        }

        bool EncodeVideoFormat(const FVideoCaptureState& capture,
                               const FScreenShotService::EAnimationFormat format,
                               const FVideoDimensions dimensions,
                               const std::string& outputPath)
        {
            if (format == FScreenShotService::EAnimationFormat::AnimatedWebp)
            {
                return EncodeAnimatedWebpWithLibwebp(capture, dimensions, outputPath);
            }

            const std::filesystem::path ffmpegPath = ResolveFfmpegPath();
            if (ffmpegPath.empty())
            {
                spdlog::error("Cannot encode three-second GIF: ffmpeg.exe is missing from the application directory");
                return false;
            }

            const std::string command = BuildFfmpegCommand(capture, ffmpegPath, dimensions, outputPath);
            const int exitCode = NextRenderer::OSProcess(command.data());
            std::error_code errorCode;
            const bool succeeded = exitCode == 0 && std::filesystem::is_regular_file(outputPath, errorCode);
            if (!succeeded)
            {
                spdlog::error("ffmpeg failed to encode {} (exit code {})", outputPath, exitCode);
            }
            return succeeded;
        }

        bool EncodeVideo(const FVideoCaptureState& capture)
        {
            FVideoDimensions dimensions;
            if (!ResolveVideoDimensions(capture, dimensions))
            {
                std::error_code errorCode;
                std::filesystem::remove_all(capture.temporaryDirectory, errorCode);
                return false;
            }

            const bool wantsGif = capture.format == FScreenShotService::EAnimationFormat::Gif ||
                capture.format == FScreenShotService::EAnimationFormat::Both;
            const bool wantsWebp = capture.format == FScreenShotService::EAnimationFormat::AnimatedWebp ||
                capture.format == FScreenShotService::EAnimationFormat::Both;

            bool gifSucceeded = !wantsGif;
            bool webpSucceeded = !wantsWebp;
            if (wantsGif)
            {
                const std::string outputPath = BuildAnimationPath(
                    capture, FScreenShotService::EAnimationFormat::Gif);
                gifSucceeded = EncodeVideoFormat(
                    capture, FScreenShotService::EAnimationFormat::Gif, dimensions, outputPath);
                if (!gifSucceeded)
                {
                    std::error_code errorCode;
                    std::filesystem::remove(outputPath, errorCode);
                }
            }

            if (wantsWebp)
            {
                const std::string outputPath = BuildAnimationPath(
                    capture, FScreenShotService::EAnimationFormat::AnimatedWebp);
                webpSucceeded = EncodeVideoFormat(
                    capture, FScreenShotService::EAnimationFormat::AnimatedWebp, dimensions, outputPath);
                if (!webpSucceeded)
                {
                    std::error_code errorCode;
                    std::filesystem::remove(outputPath, errorCode);
                }
            }

            std::error_code errorCode;
            std::filesystem::remove_all(capture.temporaryDirectory, errorCode);
            if (errorCode)
            {
                spdlog::warn("Failed to remove temporary video frames {}: {}",
                             capture.temporaryDirectory.string(), errorCode.message());
            }
            return gifSucceeded && webpSucceeded;
        }
    } // namespace

    FScreenShotService::FScreenShotService(NextEngine& engine) : engine_(engine) {}

    bool FScreenShotService::IsGifEncodingAvailable()
    {
        return !ResolveFfmpegPath().empty();
    }

    bool FScreenShotService::Request(FRequest request)
    {
        if (IsBusy())
        {
            return false;
        }

        EnsureDirectory();
        const std::string filename = BuildFilename(request.tag);
        requestPending_ = true;

        engine_.RequestScreenShot({
            .filename = filename,
            .accumulateFrames = request.accumulateFrames,
            .includeUi = request.includeUi,
            .forceUiHidden = request.forceUiHidden,
        });

        engine_.AddTickedTask([this, filename, onCompleted = std::move(request.onCompleted)](double) mutable
        {
            if (engine_.IsCapturingScreenShot())
            {
                return false;
            }

            requestPending_ = false;
            if (onCompleted)
            {
                onCompleted(filename);
            }
            return true;
        });
        return true;
    }

    bool FScreenShotService::RequestThreeSecondVideo(FThreeSecondVideoRequest request)
    {
        if (IsBusy() || videoCapture_)
        {
            return false;
        }

        const uint32_t framesPerSecond = request.framesPerSecond == 0
            ? DefaultFramesPerSecond(request.format)
            : std::min(request.framesPerSecond, kMaximumVideoFramesPerSecond);
        if (framesPerSecond == 0)
        {
            return false;
        }

        EnsureDirectory();
        const std::string stem = BuildFilename(request.tag.empty() ? "video" : request.tag);
        auto capture = std::make_shared<FVideoCaptureState>();
        capture->format = request.format;
        capture->framesPerSecond = framesPerSecond;
        capture->frameCount = static_cast<uint32_t>(kThreeSecondVideoDuration * framesPerSecond);
        capture->outputScale = request.outputScale;
        capture->includeUi = request.includeUi;
        capture->forceUiHidden = request.forceUiHidden;
        capture->outputStem = stem;
        capture->outputPath = stem +
            (request.format == FScreenShotService::EAnimationFormat::AnimatedWebp ? ".webp" : ".gif");
        capture->temporaryDirectory = std::filesystem::path(GetDirectory()) /
            (".tmp_" + std::filesystem::path(stem).filename().string());
        capture->onCaptureFinished = std::move(request.onCaptureFinished);
        capture->onCompleted = std::move(request.onCompleted);

        std::error_code errorCode;
        std::filesystem::create_directories(capture->temporaryDirectory, errorCode);
        if (errorCode)
        {
            spdlog::error("Failed to create temporary video directory {}: {}",
                          capture->temporaryDirectory.string(), errorCode.message());
            return false;
        }

        videoCapture_ = std::move(capture);
        requestPending_ = true;
        engine_.AddTickedTask([this](const double deltaSeconds)
        {
            return AdvanceThreeSecondVideo(deltaSeconds);
        });
        return true;
    }

    bool FScreenShotService::AdvanceThreeSecondVideo(const double deltaSeconds)
    {
        if (!videoCapture_)
        {
            return true;
        }

        // Scene loading mutates renderer resources and can wait for its own task
        // pipeline. Do not submit new capture requests while that transition is
        // in progress; resume the three-second timeline once the scene is running.
        if (engine_.GetEngineStatus() != NextRenderer::EApplicationStatus::Running)
        {
            return false;
        }

        videoCapture_->elapsedSeconds += std::max(deltaSeconds, 0.0);
        if (videoCapture_->frameInFlight)
        {
            if (engine_.IsCapturingScreenShot())
            {
                return false;
            }
            videoCapture_->frameInFlight = false;
        }

        if (videoCapture_->nextFrameIndex >= videoCapture_->frameCount)
        {
            auto capture = std::move(videoCapture_);
            QueueVideoEncoding(std::move(capture));
            return true;
        }

        if (videoCapture_->nextFrameIndex > 0 &&
            videoCapture_->elapsedSeconds < videoCapture_->nextFrameTime)
        {
            return false;
        }

        const uint32_t frameIndex = videoCapture_->nextFrameIndex++;
        videoCapture_->nextFrameTime = static_cast<double>(videoCapture_->nextFrameIndex) /
            static_cast<double>(videoCapture_->framesPerSecond);
        const std::filesystem::path framePath = videoCapture_->temporaryDirectory /
            fmt::format("frame_{:06}", frameIndex);
        videoCapture_->frameInFlight = true;
        engine_.RequestScreenShot({
            .filename = framePath.string(),
            .includeUi = videoCapture_->includeUi,
            .fileFormat = ScreenShot::EFileFormat::Jpeg,
            .allowOverlappingExports = true,
            .forceUiHidden = videoCapture_->forceUiHidden,
        });
        return false;
    }

    void FScreenShotService::QueueVideoEncoding(std::shared_ptr<FVideoCaptureState> capture)
    {
        if (capture->onCaptureFinished)
        {
            capture->onCaptureFinished();
        }

        capture->encodingFuture = std::async(
            std::launch::async,
            [capture]()
            {
                try
                {
                    return EncodeVideo(*capture);
                }
                catch (const std::exception& exception)
                {
                    spdlog::error("Three-second video encoding failed: {}", exception.what());
                }
                catch (...)
                {
                    spdlog::error("Three-second video encoding failed: unknown error");
                }
                return false;
            });

        engine_.AddTickedTask(
            [this, capture = std::move(capture)](double) mutable
            {
                if (capture->encodingFuture.wait_for(std::chrono::milliseconds(0)) !=
                    std::future_status::ready)
                {
                    return false;
                }

                bool succeeded = false;
                try
                {
                    succeeded = capture->encodingFuture.get();
                }
                catch (const std::exception& exception)
                {
                    spdlog::error("Three-second video encoding completion failed: {}", exception.what());
                }
                catch (...)
                {
                    spdlog::error("Three-second video encoding completion failed: unknown error");
                }

                requestPending_ = false;
                if (capture->onCompleted)
                {
                    capture->onCompleted(succeeded ? capture->outputPath : std::string{});
                }
                return true;
            });
    }

    bool FScreenShotService::IsBusy() const
    {
        return requestPending_ || engine_.IsCapturingScreenShot();
    }

    std::string FScreenShotService::GetDirectory() const
    {
        return Utilities::FileHelper::GetWritableFilePath("screenshots");
    }

    void FScreenShotService::EnsureDirectory() const
    {
        Utilities::FileHelper::EnsureDirectoryExists(GetDirectory());
    }

    std::string FScreenShotService::BuildFilename(const std::string& tag)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        const std::tm* localTime = std::localtime(&time);
        const std::string timestamp = fmt::format("{:%Y-%m-%d-%H-%M-%S}", *localTime);

        std::string stem = "screenshot_" + timestamp;
        const std::string sanitizedTag = SanitizeTag(tag);
        if (!sanitizedTag.empty())
        {
            stem += "_" + sanitizedTag;
        }

        if (stem == lastStem_)
        {
            ++stemSequence_;
        }
        else
        {
            lastStem_ = stem;
            stemSequence_ = 0;
        }

        if (stemSequence_ > 0)
        {
            stem += fmt::format("_{}", stemSequence_);
        }

        return (std::filesystem::path(GetDirectory()) / stem).string();
    }
} // namespace Runtime
