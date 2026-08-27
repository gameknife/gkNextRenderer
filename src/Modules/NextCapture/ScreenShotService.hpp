#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class NextEngine;

namespace Runtime
{
    struct FVideoCaptureState;

    class FScreenShotService final : public IScreenShotService
    {
    public:
        explicit FScreenShotService(NextEngine& engine);

        // GIF encoding shells out to ffmpeg, which is not redistributed with release
        // packages. Animated WebP is always available (libwebp is linked in). UI that
        // offers GIF recording must gate on this.
        bool IsGifEncodingAvailable() const override;

        // Requests the standard full-output screenshot. Returns false if another
        // screenshot is already queued or being saved.
        bool Request(FRequest request) override;

        // Captures exactly three seconds of frames and asynchronously encodes
        // them as a looping GIF, animated WebP, or both. Both formats reuse the
        // same temporary JPEG frame sequence. framesPerSecond == 0 uses the
        // format default (15 for GIF, 30 for animated WebP and Both).
        bool RequestThreeSecondVideo(FThreeSecondVideoRequest request) override;

        bool IsBusy() const override;
        std::string GetDirectory() const override;
        void EnsureDirectory() const override;
        void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer,
                                 const std::string& filePathWithoutExtension,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 ScreenShot::EFileFormat fileFormat,
                                 bool synchronous,
                                 std::function<void()> onCompleted,
                                 std::function<void()> onReadbackCompleted) override;

    private:
        std::string BuildFilename(const std::string& tag);
        bool AdvanceThreeSecondVideo(double deltaSeconds);
        void QueueVideoEncoding(std::shared_ptr<FVideoCaptureState> state);

        NextEngine& engine_;
        bool requestPending_ = false;
        std::shared_ptr<FVideoCaptureState> videoCapture_;
        std::string lastStem_;
        uint32_t stemSequence_ = 0;
    };
} // namespace Runtime
