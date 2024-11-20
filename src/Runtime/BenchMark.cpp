#include "BenchMark.hpp"
#include "Options.hpp"
#include "Utilities/Console.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include <fmt/format.h>
#include <fmt/chrono.h>

#include "curl/curl.h"
#include "cpp-base64/base64.cpp"
#include "ThirdParty/json11/json11.hpp"
#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <filesystem>
#include <math.h>

#include "Application.hpp"
#include "TaskCoordinator.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/SwapChain.hpp"

#if WITH_AVIF
#include "avif/avif.h"
#endif


BenchMarker::BenchMarker()
{
    std::time_t now = std::time(nullptr);
    std::string report_filename = fmt::format("report_{:%d-%m-%Y-%H-%M-%S}.csv", fmt::localtime(now));

    benchmarkCsvReportFile.open(report_filename);
    benchmarkCsvReportFile << fmt::format("#,scene,FPS\n");
}

BenchMarker::~BenchMarker()
{
    benchmarkCsvReportFile.close();
}

void BenchMarker::OnSceneStart( double nowInSeconds )
{
    periodTotalFrames_ = 0;
    benchmarkTotalFrames_ = 0;
    sceneInitialTime_ = nowInSeconds;
}

bool BenchMarker::OnTick( double nowInSeconds, Vulkan::VulkanBaseRenderer* renderer )
{
    double prevTime = time_;
    time_ = nowInSeconds;
       // Initialise scene benchmark timers
    if (periodTotalFrames_ == 0)
    {
        periodInitialTime_ = nowInSeconds;
    }

    // Print out the frame rate at regular intervals.
    {
        const double period = 1;
        const double prevTotalTime = prevTime - periodInitialTime_;
        const double totalTime = time_ - periodInitialTime_;

        if (periodTotalFrames_ != 0 && static_cast<uint64_t>(prevTotalTime / period) != static_cast<uint64_t>(totalTime
            / period))
        {
            fmt::print("\t[Benchmarking] fps: {:.0f}\n", float(periodTotalFrames_) / float(totalTime));
            periodInitialTime_ = time_;
            periodTotalFrames_ = 0;
        }

        periodTotalFrames_++;
        benchmarkTotalFrames_++;
    }

    // If in benchmark mode, bail out from the scene if we've reached the time or sample limit.
    {
        const bool timeLimitReached = GOption->BenchmarkMaxFrame == 0 && periodTotalFrames_ != 0 && time_ - sceneInitialTime_ >
            GOption->BenchmarkMaxTime;
        const bool frameLimitReached = GOption->BenchmarkMaxFrame > 0 && benchmarkTotalFrames_ >= GOption->BenchmarkMaxFrame;
        if (timeLimitReached || frameLimitReached)
        {
            return true;
        }
    }
    return false;
}

void BenchMarker::OnReport(Vulkan::VulkanBaseRenderer* renderer, const std::string& SceneName)
{
    const double totalTime = time_ - sceneInitialTime_;
    
    double fps = benchmarkTotalFrames_ / totalTime;
    fmt::print("{} totalTime {:%H:%M:%S} fps {:.3f}{}\n", CONSOLE_GOLD_COLOR, std::chrono::seconds(static_cast<long long>(totalTime)), fps, CONSOLE_DEFAULT_COLOR);
    Report(renderer, static_cast<int>(floor(fps)), SceneName, false, GOption->SaveFile);
}

void BenchMarker::SaveSwapChainToFileFast(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int inX, int inY, int inWidth, int inHeight)
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


inline const std::string versionToString(const uint32_t version)
{
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

void BenchMarker::SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int inX, int inY, int inWidth, int inHeight)
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
        std::string filename = sceneName + ".avif";
        std::ofstream file(filename, std::ios::out | std::ios::binary);
        if(file.is_open())
        {
            file.write(reinterpret_cast<const char*>(avifOutput.data), avifOutput.size);
        }
        file.close();

        // send to server
        img_encoded = base64_encode(avifOutput.data, avifOutput.size, false);
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

void BenchMarker::Report(Vulkan::VulkanBaseRenderer* renderer_, int fps, const std::string& sceneName, bool upload_screen, bool save_screen)
{
    // report file
    benchmarkCsvReportFile << fmt::format("{},{},{}\n", benchUnit_++, sceneName, fps);
    benchmarkCsvReportFile.flush();
    // screenshot
    VkPhysicalDeviceProperties deviceProp1{};
    vkGetPhysicalDeviceProperties(renderer_->Device().PhysicalDevice(), &deviceProp1);

    std::string img_encoded {};
    if (upload_screen || save_screen)
    {
        SaveSwapChainToFile(renderer_, sceneName, 0, 0, 0, 0);
    }

    // perf server upload
    if( NextRenderer::GetBuildVersion() != "v0.0.0.0" )
    {
        json11::Json my_json = json11::Json::object{
            {"renderer", renderer_->StaticClass()},
            {"scene", sceneName},
            {"gpu", std::string(deviceProp1.deviceName)},
            {"driver", versionToString(deviceProp1.driverVersion)},
            {"fps", fps},
            {"version", NextRenderer::GetBuildVersion()},
            {"screenshot", img_encoded}
        };
        std::string json_str = my_json.dump();

        fmt::print("Sending benchmark to perf server...\n");
        // upload from curl
        CURL* curl;
        CURLcode res;
        curl_global_init(CURL_GLOBAL_ALL);
        curl = curl_easy_init();
        if (curl)
        {
            curl_slist* slist1 = nullptr;
            slist1 = curl_slist_append(slist1, "Content-Type: application/json");
            slist1 = curl_slist_append(slist1, "Accept: application/json");

            /* set custom headers */
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist1);
            curl_easy_setopt(curl, CURLOPT_URL, "http://gameknife.site:60010/rt_benchmark");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
        
            /* Perform the request, res gets the return code */
            res = curl_easy_perform(curl);
            /* Check for errors */
            if (res != CURLE_OK)
                fmt::print(stderr, "curl_easy_perform() failed: {}\n", curl_easy_strerror(res));

            /* always cleanup */
            curl_easy_cleanup(curl);
        }
    }
}
