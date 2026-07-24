#include "Engine/Runtime/ScreenShot.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#if WITH_AVIF
#include "avif/avif.h"
#endif

namespace Runtime::ScreenShot
{
    namespace
    {
        struct FCaptureRect final
        {
            uint32_t x = 0;
            uint32_t y = 0;
            VkExtent2D extent{};
        };

        struct FRawScreenshot final
        {
            VkExtent2D extent{};
            uint32_t sourcePixelBytes = 0;
            uint32_t sourceRowBytes = 0;
            bool hdr10 = false;
            bool extendedLinear = false;
            std::vector<uint8_t> pixels;
        };

        float HalfToFloat(const uint16_t value)
        {
            const uint32_t sign = (value & 0x8000u) << 16u;
            int32_t exponent = static_cast<int32_t>((value >> 10u) & 0x1fu);
            uint32_t mantissa = value & 0x03ffu;

            uint32_t bits = 0;
            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign;
                }
                else
                {
                    exponent = 1;
                    while ((mantissa & 0x0400u) == 0)
                    {
                        mantissa <<= 1u;
                        --exponent;
                    }
                    mantissa &= 0x03ffu;
                    bits = sign | (static_cast<uint32_t>(exponent + 112) << 23u) | (mantissa << 13u);
                }
            }
            else if (exponent == 31)
            {
                bits = sign | 0x7f800000u | (mantissa << 13u);
            }
            else
            {
                bits = sign | (static_cast<uint32_t>(exponent + 112) << 23u) | (mantissa << 13u);
            }

            float result = 0.0f;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        }

        uint8_t LinearToSrgbByte(float linear)
        {
            linear = std::clamp(linear, 0.0f, 1.0f);
            const float srgb = linear <= 0.0031308f
                ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }

        VkSubresourceLayout GetScreenShotImageLayout(Vulkan::VulkanBaseRenderer* renderer)
        {
            VkSubresourceLayout layout{};
            const Vulkan::Image* image = renderer->GetScreenShotImage();
            if (!image)
            {
                return layout;
            }

            VkImageSubresource subresource{};
            subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource.mipLevel = 0;
            subresource.arrayLayer = 0;
            vkGetImageSubresourceLayout(renderer->Device().Handle(), image->Handle(), &subresource, &layout);
            return layout;
        }

        FCaptureRect ResolveCaptureRect(const VkExtent2D fullExtent,
                                        const int inX,
                                        const int inY,
                                        const int inWidth,
                                        const int inHeight)
        {
            const uint32_t x = static_cast<uint32_t>(std::clamp(inX, 0, static_cast<int>(fullExtent.width)));
            const uint32_t y = static_cast<uint32_t>(std::clamp(inY, 0, static_cast<int>(fullExtent.height)));
            const uint32_t availableWidth = fullExtent.width - x;
            const uint32_t availableHeight = fullExtent.height - y;

            FCaptureRect rect;
            rect.x = x;
            rect.y = y;
            rect.extent.width = inWidth > 0
                ? std::min(static_cast<uint32_t>(inWidth), availableWidth)
                : availableWidth;
            rect.extent.height = inHeight > 0
                ? std::min(static_cast<uint32_t>(inHeight), availableHeight)
                : availableHeight;
            return rect;
        }

        // The GPU copy is already complete when this function is called. Keep this
        // operation limited to a tightly packed crop so the render thread does not
        // also pay for conversion, encoding, or filesystem I/O.
        FRawScreenshot ReadbackScreenshot(Vulkan::VulkanBaseRenderer* renderer,
                                          const FCaptureRect& rect,
                                          const Vulkan::ESwapChainOutputMode outputMode)
        {
            const VkSubresourceLayout imageLayout = GetScreenShotImageLayout(renderer);
            if (imageLayout.rowPitch == 0)
            {
                Throw(std::runtime_error("screenshot image layout is unavailable"));
            }

            FRawScreenshot screenshot;
            screenshot.extent = rect.extent;
            screenshot.hdr10 = outputMode == Vulkan::ESwapChainOutputMode::HDR10_ST2084;
            screenshot.extendedLinear = outputMode == Vulkan::ESwapChainOutputMode::ExtendedSrgbLinear;
            screenshot.sourcePixelBytes = screenshot.extendedLinear ? 8u : 4u;
            screenshot.sourceRowBytes = rect.extent.width * screenshot.sourcePixelBytes;
            screenshot.pixels.resize(static_cast<size_t>(screenshot.sourceRowBytes) * rect.extent.height);

            Vulkan::DeviceMemory* memory = renderer->GetScreenShotMemory();
            if (!memory)
            {
                Throw(std::runtime_error("screenshot image memory is unavailable"));
            }

            const uint8_t* mappedData = static_cast<const uint8_t*>(memory->Map(0, VK_WHOLE_SIZE));
            const uint8_t* imageData = mappedData + imageLayout.offset;
            for (uint32_t y = 0; y < rect.extent.height; ++y)
            {
                const uint8_t* sourceRow = imageData + (rect.y + y) * imageLayout.rowPitch +
                    rect.x * screenshot.sourcePixelBytes;
                uint8_t* destinationRow = screenshot.pixels.data() +
                    static_cast<size_t>(y) * screenshot.sourceRowBytes;
                std::memcpy(destinationRow, sourceRow, screenshot.sourceRowBytes);
            }
            memory->Unmap();
            return screenshot;
        }

        template <typename ConvertFn>
        std::vector<uint8_t> ConvertScreenshot(const FRawScreenshot& screenshot,
                                               const uint32_t destinationPixelBytes,
                                               ConvertFn&& convert)
        {
            std::vector<uint8_t> destination(
                static_cast<size_t>(screenshot.extent.width) * screenshot.extent.height * destinationPixelBytes);
            uint8_t* destinationBytes = destination.data();
            for (uint32_t y = 0; y < screenshot.extent.height; ++y)
            {
                const uint8_t* sourceRow = screenshot.pixels.data() +
                    static_cast<size_t>(y) * screenshot.sourceRowBytes;
                for (uint32_t x = 0; x < screenshot.extent.width; ++x)
                {
                    convert(sourceRow + x * screenshot.sourcePixelBytes, destinationBytes);
                    destinationBytes += destinationPixelBytes;
                }
            }
            return destination;
        }

        void RemoveExistingScreenshotFile(const std::string& filename)
        {
            std::error_code errorCode;
            std::filesystem::remove(filename, errorCode);
            if (errorCode)
            {
                spdlog::warn("Failed to remove existing screenshot {} before overwrite: {}",
                             filename, errorCode.message());
            }
        }

        void EncodeAndWriteScreenshot(FRawScreenshot screenshot,
                                      const std::string& filePathWithoutExtension,
                                      const EFileFormat fileFormat)
        {
            constexpr uint32_t kComponentCount = 3;
            const size_t pixelCount = static_cast<size_t>(screenshot.extent.width) * screenshot.extent.height;
            if (pixelCount == 0)
            {
                spdlog::error("Cannot save an empty screenshot: {}", filePathWithoutExtension);
                return;
            }

            if (screenshot.hdr10)
            {
                // A2B10G10R10 -> packed RGB 10-bit-in-uint16.
                screenshot.pixels = ConvertScreenshot(screenshot, kComponentCount * sizeof(uint16_t),
                    [](const uint8_t* source, uint8_t* destination)
                    {
                        const uint32_t inputPixel = *reinterpret_cast<const uint32_t*>(source);
                        uint16_t* outputPixel = reinterpret_cast<uint16_t*>(destination);
                        outputPixel[2] = static_cast<uint16_t>((inputPixel >> 20u) & 0x3ffu);
                        outputPixel[1] = static_cast<uint16_t>((inputPixel >> 10u) & 0x3ffu);
                        outputPixel[0] = static_cast<uint16_t>(inputPixel & 0x3ffu);
                    });
            }
            else if (screenshot.extendedLinear)
            {
                // RGBA16F linear -> sRGB bytes.
                screenshot.pixels = ConvertScreenshot(screenshot, kComponentCount,
                    [](const uint8_t* source, uint8_t* destination)
                    {
                        const uint16_t* inputPixel = reinterpret_cast<const uint16_t*>(source);
                        destination[0] = LinearToSrgbByte(HalfToFloat(inputPixel[0]));
                        destination[1] = LinearToSrgbByte(HalfToFloat(inputPixel[1]));
                        destination[2] = LinearToSrgbByte(HalfToFloat(inputPixel[2]));
                    });
            }
            else
            {
                // B8G8R8A8 -> RGB bytes.
                screenshot.pixels = ConvertScreenshot(screenshot, kComponentCount,
                    [](const uint8_t* source, uint8_t* destination)
                    {
                        const uint32_t inputPixel = *reinterpret_cast<const uint32_t*>(source);
                        destination[0] = static_cast<uint8_t>((inputPixel >> 16u) & 0xffu);
                        destination[1] = static_cast<uint8_t>((inputPixel >> 8u) & 0xffu);
                        destination[2] = static_cast<uint8_t>(inputPixel & 0xffu);
                    });
            }

#if WITH_AVIF
            if (fileFormat == EFileFormat::Automatic)
            {
            avifImage* image = avifImageCreate(
                screenshot.extent.width,
                screenshot.extent.height,
                screenshot.hdr10 ? 10 : 8,
                AVIF_PIXEL_FORMAT_YUV444);
            if (!image)
            {
                spdlog::error("Failed to create AVIF image: {}", filePathWithoutExtension);
                return;
            }

            image->yuvRange = AVIF_RANGE_FULL;
            image->colorPrimaries = screenshot.hdr10 ? AVIF_COLOR_PRIMARIES_BT2020 : AVIF_COLOR_PRIMARIES_BT709;
            image->transferCharacteristics = screenshot.hdr10
                ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084
                : AVIF_TRANSFER_CHARACTERISTICS_BT709;
            image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
            image->clli.maxCLL = 600;
            image->clli.maxPALL = 0;

            avifRGBImage rgbImage{};
            avifRGBImageSetDefaults(&rgbImage, image);
            rgbImage.format = AVIF_RGB_FORMAT_RGB;
            rgbImage.ignoreAlpha = AVIF_TRUE;
            rgbImage.pixels = screenshot.pixels.data();
            rgbImage.rowBytes = screenshot.extent.width * kComponentCount * (screenshot.hdr10 ? sizeof(uint16_t) : sizeof(uint8_t));

            const avifResult convertResult = avifImageRGBToYUV(image, &rgbImage);
            if (convertResult != AVIF_RESULT_OK)
            {
                spdlog::error("Failed to convert screenshot to AVIF: {} ({})",
                              filePathWithoutExtension, avifResultToString(convertResult));
                avifImageDestroy(image);
                return;
            }

            avifEncoder* encoder = avifEncoderCreate();
            if (!encoder)
            {
                spdlog::error("Failed to create AVIF encoder: {}", filePathWithoutExtension);
                avifImageDestroy(image);
                return;
            }

            encoder->quality = 80;
            encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
            encoder->speed = AVIF_SPEED_FASTEST;
            avifRWData output = AVIF_DATA_EMPTY;
            const avifResult addImageResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
            const avifResult encodeResult = addImageResult == AVIF_RESULT_OK
                ? avifEncoderFinish(encoder, &output)
                : addImageResult;
            if (encodeResult != AVIF_RESULT_OK)
            {
                spdlog::error("Failed to encode screenshot as AVIF: {} ({})",
                              filePathWithoutExtension, avifResultToString(encodeResult));
                avifRWDataFree(&output);
                avifEncoderDestroy(encoder);
                avifImageDestroy(image);
                return;
            }

            const std::string filename = filePathWithoutExtension + ".avif";
            RemoveExistingScreenshotFile(filename);
            std::ofstream file(filename, std::ios::out | std::ios::binary | std::ios::trunc);
            if (file.is_open())
            {
                file.write(reinterpret_cast<const char*>(output.data), static_cast<std::streamsize>(output.size));
            }
            else
            {
                spdlog::error("Failed to open screenshot file: {}", filename);
            }
            file.close();
            avifRWDataFree(&output);
            avifEncoderDestroy(encoder);
            avifImageDestroy(image);
            }
            else
#endif
            {
            const std::string filename = filePathWithoutExtension + ".jpg";
            RemoveExistingScreenshotFile(filename);
            if (screenshot.hdr10)
            {
                const uint16_t* hdrData = reinterpret_cast<const uint16_t*>(screenshot.pixels.data());
                std::vector<uint8_t> sdrData(pixelCount * kComponentCount);
                for (size_t index = 0; index < sdrData.size(); ++index)
                {
                    float scaled = static_cast<float>(hdrData[index]) / 1300.0f * 2.0f;
                    scaled = scaled * scaled * 255.0f;
                    sdrData[index] = static_cast<uint8_t>(std::min(scaled, 255.0f));
                }
                if (stbi_write_jpg(filename.c_str(), static_cast<int>(screenshot.extent.width),
                                   static_cast<int>(screenshot.extent.height), kComponentCount,
                                   sdrData.data(), 91) == 0)
                {
                    spdlog::error("Failed to write screenshot: {}", filename);
                }
            }
            else if (stbi_write_jpg(filename.c_str(), static_cast<int>(screenshot.extent.width),
                                    static_cast<int>(screenshot.extent.height), kComponentCount,
                                    screenshot.pixels.data(), 91) == 0)
            {
                spdlog::error("Failed to write screenshot: {}", filename);
            }
            }
        }
    } // namespace

    void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer,
                             const std::string& filePathWithoutExtension,
                             const int inX,
                             const int inY,
                             const int inWidth,
                             const int inHeight,
                             const EFileFormat fileFormat,
                             const bool synchronous,
                             std::function<void()> onCompleted,
                             std::function<void()> onReadbackCompleted)
    {
        const Vulkan::SwapChain& swapChain = renderer->SwapChain();
        const VkExtent2D fullExtent = swapChain.Extent();
        const FCaptureRect rect = ResolveCaptureRect(fullExtent, inX, inY, inWidth, inHeight);
        if (rect.extent.width == 0 || rect.extent.height == 0)
        {
            Throw(std::runtime_error("screenshot crop is empty"));
        }

        renderer->CaptureScreenShot();
        FRawScreenshot screenshot = ReadbackScreenshot(renderer, rect, swapChain.OutputMode());
        if (onReadbackCompleted)
        {
            onReadbackCompleted();
        }

        auto encodeAndWrite = [screenshot = std::move(screenshot), filePathWithoutExtension, fileFormat]() mutable
        {
            try
            {
                EncodeAndWriteScreenshot(std::move(screenshot), filePathWithoutExtension, fileFormat);
            }
            catch (const std::exception& exception)
            {
                spdlog::error("Failed to save screenshot {}: {}", filePathWithoutExtension, exception.what());
            }
            catch (...)
            {
                spdlog::error("Failed to save screenshot {}: unknown error", filePathWithoutExtension);
            }
        };

        if (synchronous)
        {
            encodeAndWrite();
            if (onCompleted)
            {
                onCompleted();
            }
            return;
        }

        // Keep regular interactive captures off the render thread. Agent validation passes
        // synchronous=true so the caller can safely exit immediately after this function returns.
        Tasks::TaskCoordinator::GetInstance()->AddTask(
            [encodeAndWrite = std::move(encodeAndWrite)](Tasks::ResTask&) mutable { encodeAndWrite(); },
            [onCompleted = std::move(onCompleted)](Tasks::ResTask&) mutable
            {
                if (onCompleted)
                {
                    onCompleted();
                }
            },
            1);
    }
}
