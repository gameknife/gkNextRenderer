#include "ScreenShot.hpp"
#include "Utilities/Console.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"

#include "curl/curl.h"
#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <filesystem>
#include "TaskCoordinator.hpp"
#include "Vulkan/SwapChain.hpp"

#if WITH_AVIF
#include "avif/avif.h"
#endif

namespace ScreenShot
{
    void SaveSwapChainToFileFast(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int inX, int inY, int inWidth, int inHeight)
    {
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = renderer_->SwapChain();
        auto orgExtent = swapChain.Extent();
        auto extent = swapChain.Extent();

        if(inWidth > 0 && inHeight > 0)
        {
            extent.width = inWidth;
            extent.height = inHeight;
        }

        // capture and export
        renderer_->CaptureScreenShot();
    
        uint32_t dataBytes = 0;
        uint32_t rowBytes = 0;
        constexpr uint32_t kCompCnt = 3;
        dataBytes = extent.width * extent.height * kCompCnt;
        uint32_t rawDataBytes = extent.width * extent.height * 4;
        rowBytes = extent.width * 3 * sizeof(uint8_t);
    
        Vulkan::DeviceMemory* vkMemory = renderer_->GetScreenShotMemory();
        uint8_t* mappedGPUData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);
        uint8_t* mappedData = (uint8_t*)malloc(rawDataBytes);
        memcpy(mappedData, mappedGPUData, rawDataBytes);
        vkMemory->Unmap();
        TaskCoordinator::GetInstance()->AddTask([=](ResTask& task)->void
        {
            uint8_t* dataview = (uint8_t*)malloc(dataBytes);
            {
                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t srcYDelta = orgExtent.width * 4;
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
        [](ResTask& task)
        {

        },1);
    }

    void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int inX, int inY, int inWidth, int inHeight)
    {
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = renderer_->SwapChain();

        auto orgExtent = swapChain.Extent();
        auto extent = swapChain.Extent();

        if(inWidth > 0 && inHeight > 0)
        {
            extent.width = inWidth;
            extent.height = inHeight;
        }

        // capture and export
        renderer_->CaptureScreenShot();

        // too slow on main thread, copy out buffer and use thread to save
    
        // prepare data
        void* data = nullptr;
        uint32_t dataBytes = 0;
        uint32_t rowBytes = 0;

        constexpr uint32_t kCompCnt = 3;
        if(swapChain.IsHDR())
        {
            dataBytes = extent.width * extent.height * 3 * 2;
            rowBytes = extent.width * 3 * sizeof(uint16_t);
            data = malloc(dataBytes);
            
            uint16_t* dataview = (uint16_t*)data;
            {
                Vulkan::DeviceMemory* vkMemory = renderer_->GetScreenShotMemory();
                uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);

                uint32_t srcYDelta = orgExtent.width * 4;
                uint32_t srcXDelta = 4;
            
                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
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
                        dataview[yy + xx + 2] = (uInPixel & (0b1111111111 << 20)) >> 20;
                        dataview[yy + xx + 1] = (uInPixel & (0b1111111111 << 10)) >> 10;
                        dataview[yy + xx + 0] = (uInPixel & (0b1111111111 << 0)) >> 0;
                        
                        srcX += srcXDelta;
                        xx += xDelta;
                    }
                    srcY += srcYDelta;
                    yy += yDelta;
                }
                vkMemory->Unmap();
            }
        }
        else
        {
            dataBytes = extent.width * extent.height * kCompCnt;
            rowBytes = extent.width * 3 * sizeof(uint8_t);
            data = malloc(dataBytes);
            
            uint8_t* dataview = (uint8_t*)data;
            {
                Vulkan::DeviceMemory* vkMemory = renderer_->GetScreenShotMemory();
                uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);
                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t srcYDelta = orgExtent.width * 4;
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
                vkMemory->Unmap();
            }
        }
        
#if WITH_AVIF
        avifImage* image = avifImageCreate(extent.width, extent.height, swapChain.IsHDR() ? 10 : 8, AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF
        if (!image)
        {
            Throw(std::runtime_error("avif image creation failed"));
        }
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = swapChain.IsHDR() ? AVIF_COLOR_PRIMARIES_BT2020 : AVIF_COLOR_PRIMARIES_BT709;
        image->transferCharacteristics = swapChain.IsHDR() ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 : AVIF_TRANSFER_CHARACTERISTICS_BT709;
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
        if(swapChain.IsHDR())
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
