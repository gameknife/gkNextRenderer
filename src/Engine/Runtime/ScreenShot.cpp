#include "Engine/Runtime/ScreenShot.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Exception.hpp"

#include "curl/curl.h"
#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#if WITH_AVIF
#include "avif/avif.h"
#endif

namespace Runtime::ScreenShot
{
    namespace
    {
        float HalfToFloat(uint16_t value)
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
            vkGetImageSubresourceLayout(
                renderer->Device().Handle(),
                image->Handle(),
                &subresource,
                &layout);
            return layout;
        }

        // Copies the captured swapchain rect into a tightly packed buffer, invoking
        // convert(srcPixel, dstPixel) per pixel; dst advances by dstPixelBytes.
        template <typename ConvertFn>
        void ConvertScreenshotRect(Vulkan::VulkanBaseRenderer* renderer, const VkSubresourceLayout& imageLayout,
                                   const VkExtent2D extent, const int inX, const int inY,
                                   const uint32_t srcPixelBytes, const uint32_t dstPixelBytes, void* dst,
                                   ConvertFn&& convert)
        {
            Vulkan::DeviceMemory* vkMemory = renderer->GetScreenShotMemory();
            uint8_t* mappedData = static_cast<uint8_t*>(vkMemory->Map(0, VK_WHOLE_SIZE));
            const uint8_t* imageData = mappedData + imageLayout.offset;
            const uint32_t srcRowBytes = static_cast<uint32_t>(imageLayout.rowPitch);

            uint8_t* dstBytes = static_cast<uint8_t*>(dst);
            for (uint32_t y = 0; y < extent.height; y++)
            {
                const uint8_t* srcRow = imageData + (inY + y) * srcRowBytes + inX * srcPixelBytes;
                for (uint32_t x = 0; x < extent.width; x++)
                {
                    convert(srcRow + x * srcPixelBytes, dstBytes);
                    dstBytes += dstPixelBytes;
                }
            }
            vkMemory->Unmap();
        }
    }

    void SaveSwapChainToFileFast(Vulkan::VulkanBaseRenderer* renderer, const std::string& filePathWithoutExtension, int inX, int inY, int inWidth, int inHeight)
    {
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = renderer->SwapChain();
        if (swapChain.IsHDR())
        {
            // The fast path assumes 8-bit SDR swapchain pixels. HDR swapchains use packed 10-bit output,
            // so re-use the standard path's tone-mapped export to avoid color corruption.
            SaveSwapChainToFile(renderer, filePathWithoutExtension, inX, inY, inWidth, inHeight);
            return;
        }

        auto orgExtent = swapChain.Extent();
        auto extent = swapChain.Extent();

        if(inWidth > 0 && inHeight > 0)
        {
            extent.width = inWidth;
            extent.height = inHeight;
        }

        // capture and export
        renderer->CaptureScreenShot();
    
        uint32_t dataBytes = 0;
        uint32_t rowBytes = 0;
        constexpr uint32_t kCompCnt = 3;
        dataBytes = extent.width * extent.height * kCompCnt;
        rowBytes = extent.width * 3 * sizeof(uint8_t);
    
        Vulkan::DeviceMemory* vkMemory = renderer->GetScreenShotMemory();
        const VkSubresourceLayout imageLayout = GetScreenShotImageLayout(renderer);
        const uint32_t srcRowBytes = static_cast<uint32_t>(imageLayout.rowPitch);
        const uint32_t rawDataBytes = srcRowBytes * orgExtent.height;
        uint8_t* mappedGPUData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);
        uint8_t* mappedData = (uint8_t*)malloc(rawDataBytes);
        memcpy(mappedData, mappedGPUData + imageLayout.offset, rawDataBytes);
        vkMemory->Unmap();
        Tasks::TaskCoordinator::GetInstance()->AddTask([=](Tasks::ResTask& task)->void
        {
            uint8_t* dataview = (uint8_t*)malloc(dataBytes);
            {
                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t srcYDelta = srcRowBytes;
                uint32_t srcXDelta = 4;
            
                uint32_t yy = 0;
                uint32_t xx = 0;
                uint32_t srcY = inY * srcYDelta;
                uint32_t srcX = inX * srcXDelta;
            
                for (uint32_t y = 0; y < extent.height; y++)
                {
                    xx = 0;
                    srcX = inX * srcXDelta;
                    for (uint32_t x = 0; x < extent.width; x++)
                    {
                        uint32_t* pInPixel = (uint32_t*)&mappedData[srcY + srcX];
                        uint32_t uInPixel = *pInPixel;
                        dataview[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                        dataview[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                        dataview[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;
        
                        srcX += srcXDelta;
                        xx += xDelta;
                    }
                    srcY += srcYDelta;
                    yy += yDelta;
                }
            }
            std::string filename = filePathWithoutExtension + ".jpg";
            stbi_write_jpg(filename.c_str(), extent.width, extent.height, kCompCnt, dataview, 91);
        
            free(dataview);
            free(mappedData);
        },
        [](Tasks::ResTask& task)
        {

        },1);
    }

    void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer, const std::string& filePathWithoutExtension, int inX, int inY, int inWidth, int inHeight)
    {
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = renderer->SwapChain();

        auto orgExtent = swapChain.Extent();
        auto extent = swapChain.Extent();

        if(inWidth > 0 && inHeight > 0)
        {
            extent.width = inWidth;
            extent.height = inHeight;
        }

        // capture and export
        renderer->CaptureScreenShot();

        // too slow on main thread, copy out buffer and use thread to save
    
        // prepare data
        void* data = nullptr;
        uint32_t dataBytes = 0;
        uint32_t rowBytes = 0;
        const VkSubresourceLayout imageLayout = GetScreenShotImageLayout(renderer);
        const bool hdr10Screenshot = swapChain.OutputMode() == Vulkan::ESwapChainOutputMode::HDR10_ST2084;
        const bool extendedLinearScreenshot =
            swapChain.OutputMode() == Vulkan::ESwapChainOutputMode::ExtendedSrgbLinear;

        constexpr uint32_t kCompCnt = 3;
        if(hdr10Screenshot)
        {
            // A2B10G10R10 -> packed RGB 10-bit-in-uint16.
            dataBytes = extent.width * extent.height * kCompCnt * sizeof(uint16_t);
            rowBytes = extent.width * kCompCnt * sizeof(uint16_t);
            data = malloc(dataBytes);
            ConvertScreenshotRect(renderer, imageLayout, extent, inX, inY, 4, kCompCnt * sizeof(uint16_t), data,
                [](const uint8_t* src, uint8_t* dst)
                {
                    const uint32_t uInPixel = *reinterpret_cast<const uint32_t*>(src);
                    uint16_t* outPixel = reinterpret_cast<uint16_t*>(dst);
                    outPixel[2] = (uInPixel & (0b1111111111 << 20)) >> 20;
                    outPixel[1] = (uInPixel & (0b1111111111 << 10)) >> 10;
                    outPixel[0] = (uInPixel & (0b1111111111 << 0)) >> 0;
                });
        }
        else if (extendedLinearScreenshot)
        {
            // RGBA16F linear -> sRGB bytes.
            dataBytes = extent.width * extent.height * kCompCnt;
            rowBytes = extent.width * kCompCnt * sizeof(uint8_t);
            data = malloc(dataBytes);
            ConvertScreenshotRect(renderer, imageLayout, extent, inX, inY, 8, kCompCnt, data,
                [](const uint8_t* src, uint8_t* dst)
                {
                    const uint16_t* inPixel = reinterpret_cast<const uint16_t*>(src);
                    dst[0] = LinearToSrgbByte(HalfToFloat(inPixel[0]));
                    dst[1] = LinearToSrgbByte(HalfToFloat(inPixel[1]));
                    dst[2] = LinearToSrgbByte(HalfToFloat(inPixel[2]));
                });
        }
        else
        {
            // B8G8R8A8 -> RGB bytes.
            dataBytes = extent.width * extent.height * kCompCnt;
            rowBytes = extent.width * kCompCnt * sizeof(uint8_t);
            data = malloc(dataBytes);
            ConvertScreenshotRect(renderer, imageLayout, extent, inX, inY, 4, kCompCnt, data,
                [](const uint8_t* src, uint8_t* dst)
                {
                    const uint32_t uInPixel = *reinterpret_cast<const uint32_t*>(src);
                    dst[0] = (uInPixel & (0b11111111 << 16)) >> 16;
                    dst[1] = (uInPixel & (0b11111111 << 8)) >> 8;
                    dst[2] = (uInPixel & (0b11111111 << 0)) >> 0;
                });
        }
        
#if WITH_AVIF
        avifImage* image = avifImageCreate(extent.width, extent.height, hdr10Screenshot ? 10 : 8, AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF
        if (!image)
        {
            Throw(std::runtime_error("avif image creation failed"));
        }
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = hdr10Screenshot ? AVIF_COLOR_PRIMARIES_BT2020 : AVIF_COLOR_PRIMARIES_BT709;
        image->transferCharacteristics = hdr10Screenshot ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 : AVIF_TRANSFER_CHARACTERISTICS_BT709;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        image->clli.maxCLL = static_cast<uint16_t>(600); //maxCLLNits;
        image->clli.maxPALL = 0; //maxFALLNits;

        avifEncoder* encoder = NULL;
        avifRWData avifOutput = AVIF_DATA_EMPTY;

        avifRGBImage rgbAvifImage{};
        avifRGBImageSetDefaults(&rgbAvifImage, image);
        rgbAvifImage.format = AVIF_RGB_FORMAT_RGB;
        rgbAvifImage.ignoreAlpha = AVIF_TRUE;
        rgbAvifImage.pixels = (uint8_t*)data;
        rgbAvifImage.rowBytes = rowBytes;

        avifResult convertResult = avifImageRGBToYUV(image, &rgbAvifImage);
        if (convertResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to convert RGB to YUV: " + std::string(avifResultToString(convertResult))));
        }
        encoder = avifEncoderCreate();
        if (!encoder)
        {
            Throw(std::runtime_error("Failed to create encoder"));
        }
        encoder->quality = 80;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
        encoder->speed = AVIF_SPEED_FASTEST;

        avifResult addImageResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
        if (addImageResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to add image: " + std::string(avifResultToString(addImageResult))));
        }
        avifResult finishResult = avifEncoderFinish(encoder, &avifOutput);
        if (finishResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to finish encoding: " + std::string(avifResultToString(finishResult))));
        }

        // save to file with scenename
        std::string filename = filePathWithoutExtension + ".avif";
        std::ofstream file(filename, std::ios::out | std::ios::binary);
        if(file.is_open())
        {
            file.write(reinterpret_cast<const char*>(avifOutput.data), avifOutput.size);
        }
        file.close();

        // send to server
        //img_encoded = base64_encode(avifOutput.data, avifOutput.size, false);
#else
        // save to file with scenename
        std::string filename = filePathWithoutExtension + ".jpg";
        
        // if hdr, transcode 16bit to 8bit
        if(hdr10Screenshot)
        {
            uint16_t* dataview = (uint16_t*)data;
            uint8_t* sdrData = (uint8_t*)malloc(extent.width * extent.height * kCompCnt);
            for ( uint32_t i = 0; i < extent.width * extent.height * kCompCnt; i++ )
            {
                float scaled = dataview[i] / 1300.f * 2.0f;
                scaled = scaled * scaled;
                scaled *= 255.f;
                sdrData[i] = (uint8_t)(std::min(scaled, 255.f));
            }
            stbi_write_jpg(filename.c_str(), extent.width, extent.height, kCompCnt, (const void*)sdrData, 91);
            free(sdrData);
        }
        else
        {
            stbi_write_jpg(filename.c_str(), extent.width, extent.height, kCompCnt, (const void*)data, 91);
        }
#endif
        free(data);
    }
}
