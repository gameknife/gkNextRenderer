#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

class NextEngine;

namespace Runtime::ScreenShot
{
    enum class EFileFormat
    {
        Automatic,
        Jpeg,
    };
}

namespace Runtime
{
    class IScreenShotService
    {
    public:
        enum class EAnimationFormat
        {
            Gif,
            AnimatedWebp,
            Both,
        };

        enum class EVideoOutputScale
        {
            Full,
            Half,
            Quarter,
        };

        struct FRequest
        {
            std::string tag;
            uint32_t accumulateFrames = 0;
            bool includeUi = false;
            std::function<void(const std::string& path)> onCompleted;
            bool forceUiHidden = false;
        };

        struct FThreeSecondVideoRequest
        {
            std::string tag;
            EAnimationFormat format = EAnimationFormat::Both;
            uint32_t framesPerSecond = 0;
            EVideoOutputScale outputScale = EVideoOutputScale::Half;
            bool includeUi = false;
            bool forceUiHidden = false;
            std::function<void()> onCaptureFinished;
            std::function<void(const std::string& path)> onCompleted;
        };

        virtual ~IScreenShotService() = default;

        virtual bool IsGifEncodingAvailable() const = 0;
        virtual bool Request(FRequest request) = 0;
        bool Request() { return Request(FRequest{}); }
        virtual bool RequestThreeSecondVideo(FThreeSecondVideoRequest request) = 0;
        bool RequestThreeSecondVideo() { return RequestThreeSecondVideo(FThreeSecondVideoRequest{}); }
        virtual bool IsBusy() const = 0;
        virtual std::string GetDirectory() const = 0;
        virtual void EnsureDirectory() const = 0;

        virtual void SaveSwapChainToFile(
            Vulkan::VulkanBaseRenderer* renderer,
            const std::string& filePathWithoutExtension,
            int x,
            int y,
            int width,
            int height,
            ScreenShot::EFileFormat fileFormat,
            bool synchronous,
            std::function<void()> onCompleted,
            std::function<void()> onReadbackCompleted) = 0;
    };
}
