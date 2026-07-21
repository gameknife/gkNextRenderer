#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <vulkan/vulkan.h>

namespace Assets
{
    struct UniformBufferObject;
}

namespace Rendering::Upscaler
{
    enum class EUpscalerProvider : uint32_t
    {
        None,
        Streamline,
        FidelityFX,
    };

    enum class EUpscaleMode : uint32_t
    {
        Quality = 0,
        Balanced = 1,
        Performance = 2,
        UltraPerformance = 3,
        Native = 4,
        Auto = 5,
    };

    struct FUpscaleModeInfo
    {
        EUpscaleMode mode = EUpscaleMode::Quality;
        const char* name = "Quality";
        float fallbackScale = 1.5f;
    };

    inline const FUpscaleModeInfo& GetUpscaleModeInfo(uint32_t rawMode)
    {
        static constexpr FUpscaleModeInfo kModes[] = {
            {EUpscaleMode::Quality, "Quality", 1.5f},
            {EUpscaleMode::Balanced, "Balanced", 1.7f},
            {EUpscaleMode::Performance, "Performance", 2.0f},
            {EUpscaleMode::UltraPerformance, "Ultra Performance", 3.0f},
            {EUpscaleMode::Native, "Native", 1.0f},
            // Auto has no fixed scale. ResolveUpscaleMode converts it to either
            // Native/DLAA or Quality before the provider sees it.
            {EUpscaleMode::Auto, "Auto", 1.0f},
        };

        if (rawMode >= std::size(kModes))
        {
            return kModes[0];
        }
        return kModes[rawMode];
    }

    struct FResolvedUpscaleMode
    {
        bool enabled = false;
        uint32_t mode = static_cast<uint32_t>(EUpscaleMode::Native);
    };

    inline FResolvedUpscaleMode ResolveUpscaleMode(uint32_t rawMode, VkExtent2D outputExtent)
    {
        const auto& modeInfo = GetUpscaleModeInfo(rawMode);
        if (modeInfo.mode != EUpscaleMode::Auto)
        {
            return {true, static_cast<uint32_t>(modeInfo.mode)};
        }

        constexpr uint64_t fullHdPixelCount = 1920ull * 1080ull;
        const uint64_t outputPixelCount =
            static_cast<uint64_t>(outputExtent.width) * static_cast<uint64_t>(outputExtent.height);
        if (outputPixelCount <= fullHdPixelCount)
        {
            return {true, static_cast<uint32_t>(EUpscaleMode::Native)};
        }

        return {true, static_cast<uint32_t>(EUpscaleMode::Quality)};
    }

    inline VkExtent2D ScaleExtent(VkExtent2D extent, float scale)
    {
        scale = std::max(scale, 1.0f);
        return {
            std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.width) / scale)),
            std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.height) / scale)),
        };
    }

    struct FReprojectionTransforms
    {
        glm::mat4 clipToPrevClip{1.0f};
        glm::mat4 prevClipToClip{1.0f};
    };

    inline FReprojectionTransforms CalculateReprojectionTransforms(
        const glm::mat4& projection,
        const glm::mat4& view,
        const glm::mat4& previousViewProjection)
    {
        FReprojectionTransforms result{};
        result.clipToPrevClip = previousViewProjection * glm::inverse(view) * glm::inverse(projection);
        result.prevClipToClip = glm::inverse(result.clipToPrevClip);
        return result;
    }

    inline glm::vec2 CalculateMotionVectorScale(VkExtent2D renderExtent)
    {
        return {
            1.0f / static_cast<float>(std::max(1u, renderExtent.width)),
            1.0f / static_cast<float>(std::max(1u, renderExtent.height)),
        };
    }

    struct FFeatureCaps
    {
        EUpscalerProvider provider = EUpscalerProvider::None;
        bool streamlineInitialized = false;
        bool streamlineDeviceReady = false;
        bool supportDLSS = false;
        bool supportDLSSRR = false;
        bool supportDLSSG = false;
        bool supportReflex = false;
        bool supportPCL = false;
        bool supportFSR = false;
        bool supportFSRFrameGeneration = false;
        bool requestedDeviceExtensionsAvailable = false;
        uint32_t requiredGraphicsQueues = 0;
        uint32_t requiredComputeQueues = 0;
        uint32_t requiredOpticalFlowQueues = 0;
    };

    struct FDeviceInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        uint32_t computeQueueIndex = 0;
        uint32_t computeQueueFamily = 0;
        uint32_t graphicsQueueIndex = 0;
        uint32_t graphicsQueueFamily = 0;
        uint32_t opticalFlowQueueIndex = 0;
        uint32_t opticalFlowQueueFamily = UINT32_MAX;
        bool useNativeOpticalFlowMode = false;
    };

    enum class EFrameMarker
    {
        SimulationStart,
        SimulationEnd,
        RenderSubmitStart,
        RenderSubmitEnd,
        PresentStart,
        PresentEnd,
    };

    struct FFrameToken
    {
        void* native = nullptr;
        uint32_t frameIndex = 0;

        explicit operator bool() const
        {
            return native != nullptr;
        }
    };

    struct FOptimalRenderSettings
    {
        VkExtent2D renderExtent{};
        VkExtent2D minRenderExtent{};
        VkExtent2D maxRenderExtent{};
        bool fromStreamline = false;
    };

    struct FImageResource
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageUsageFlags usage = 0;

        bool IsValid() const
        {
            return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE && extent.width > 0 && extent.height > 0;
        }
    };

    struct FCameraConstants
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float verticalFovRadians = 1.0f;
        float aspectRatio = 1.0f;
    };

    struct FFrameGenerationState
    {
        bool valid = false;
        uint32_t numFramesActuallyPresented = 0;
        uint32_t numFramesToGenerateMax = 1;
        uint32_t minWidthOrHeight = 0;
        uint64_t estimatedVRAMUsageInBytes = 0;
        uint32_t statusMask = 0;
    };

    struct FFrameInputs
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        FFrameToken frameToken{};
        uint32_t frameIndex = 0;
        uint32_t imageIndex = 0;
        bool reset = false;
        bool enableDLSS = false;
        bool enableDLSSRR = false;
        bool enableDLSSG = false;
        bool enableFSR = false;
        bool enableFSRFrameGeneration = false;
        uint32_t superResolutionMode = 0;
        uint32_t frameGenerationMultiplier = 2;
        bool hdrOutput = false;
        float frameTimeDeltaMilliseconds = 16.6667f;

        VkExtent2D renderExtent{};
        VkExtent2D outputExtent{};
        VkOffset2D outputOffset{};
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        uint32_t backBufferCount = 0;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;

        const Assets::UniformBufferObject* ubo = nullptr;
        FCameraConstants camera{};

        FImageResource depth;
        FImageResource motionVectors;
        FImageResource scalingInputColor;
        FImageResource scalingOutputColor;
        FImageResource hudlessColor;
        FImageResource uiColorAndAlpha;

        FImageResource albedo;
        FImageResource specularAlbedo;
        FImageResource normalRoughness;
        FImageResource diffuseHitDistance;
        FImageResource specularHitDistance;
        FImageResource diffuseNoisy;
        FImageResource specularNoisy;
    };
}
