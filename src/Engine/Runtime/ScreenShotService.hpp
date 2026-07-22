#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class NextEngine;

namespace Runtime
{
    struct FVideoCaptureState;

    class FScreenShotService final
    {
    public:
        enum class EAnimationFormat
        {
            Gif,
            AnimatedWebp,
        };

        struct FRequest
        {
            std::string tag;
            uint32_t accumulateFrames = 0;
            bool includeUi = false;
            std::function<void(const std::string& path)> onCompleted;
        };

        struct FThreeSecondVideoRequest
        {
            std::string tag;
            EAnimationFormat format = EAnimationFormat::Gif;
            uint32_t framesPerSecond = 0;
            bool includeUi = false;
            // Called on the engine thread after the three-second frame capture
            // is complete and before GIF/WebP encoding starts.
            std::function<void()> onCaptureFinished;
            // Receives the final path on success, or an empty string on failure.
            std::function<void(const std::string& path)> onCompleted;
        };

        explicit FScreenShotService(NextEngine& engine);

        // Requests the standard full-output screenshot. Returns false if another
        // screenshot is already queued or being saved.
        bool Request(FRequest request = {});

        // Captures exactly three seconds of frames and asynchronously encodes
        // them as a looping GIF or animated WebP. framesPerSecond == 0 uses
        // the format default (15 for GIF, 30 for animated WebP).
        bool RequestThreeSecondVideo(FThreeSecondVideoRequest request = {});

        bool IsBusy() const;
        std::string GetDirectory() const;
        void EnsureDirectory() const;

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
