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
#include "Utilities/Math.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/SwapChain.hpp"

#if WITH_AVIF
#include "avif/avif.h"
#endif


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
        if (benchmarkNumber_ == 0)
        {
            std::time_t now = std::time(nullptr);
            std::string report_filename = fmt::format("report_{:%d-%m-%Y-%H-%M-%S}.csv", fmt::localtime(now));
            //benchmarkCsvReportFile.open(report_filename);
            //benchmarkCsvReportFile << fmt::format("#;scene;FPS\n");
        }

        //fmt::print("\n\nRenderer: {}\n", renderer->GetActualClassName());
        //fmt::print("Benchmark: Start scene #{} '{}'\n", sceneIndex_, SceneList::AllScenes[sceneIndex_].first);
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
            benchmarkNumber_++;
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

void BenchMarker::OnReport()
{
    const double totalTime = time_ - sceneInitialTime_;
    
    double fps = benchmarkTotalFrames_ / totalTime;
    fmt::print("{} totalTime {:%H:%M:%S} fps {:.3f}{}\n", CONSOLE_GOLD_COLOR, std::chrono::seconds(static_cast<long long>(totalTime)), fps, CONSOLE_DEFAULT_COLOR);

    //std::string SceneName = SceneList::AllScenes[userSettings_.SceneIndex].first;
    //Report(static_cast<int>(floor(fps)), SceneName, false, GOption->SaveFile);
    //benchmarkCsvReportFile << fmt::format("{};{};{:.3f}\n", sceneIndex_, SceneList::AllScenes[sceneIndex_].first, fps);
}


inline const std::string versionToString(const uint32_t version)
{
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

void BenchMarker::Report(Vulkan::VulkanBaseRenderer* renderer_, int fps, const std::string& sceneName, bool upload_screen, bool save_screen)
{
    VkPhysicalDeviceProperties deviceProp1{};
    vkGetPhysicalDeviceProperties(renderer_->Device().PhysicalDevice(), &deviceProp1);

    std::string img_encoded {};
    if (upload_screen || save_screen)
    {
#if WITH_AVIF
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = renderer_->SwapChain();
        const auto extent = swapChain.Extent();

        // capture and export
        renderer_->CaptureScreenShot();

        constexpr uint32_t kCompCnt = 3;
        int imageSize = extent.width * extent.height * kCompCnt;

        avifImage* image = avifImageCreate(extent.width, extent.height, swapChain.IsHDR() ? 10 : 8, AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF
        if (!image)
        {
            Throw(std::runtime_error("avif image creation failed"));
        }
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = swapChain.IsHDR() ? AVIF_COLOR_PRIMARIES_BT2020 : AVIF_COLOR_PRIMARIES_BT709;
        image->transferCharacteristics = swapChain.IsHDR() ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 : AVIF_TRANSFER_CHARACTERISTICS_BT709;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        image->clli.maxCLL = static_cast<uint16_t>(userSettings_.PaperWhiteNit); //maxCLLNits;
        image->clli.maxPALL = 0; //maxFALLNits;

        avifEncoder* encoder = NULL;
        avifRWData avifOutput = AVIF_DATA_EMPTY;

        avifRGBImage rgbAvifImage{};
        avifRGBImageSetDefaults(&rgbAvifImage, image);
        rgbAvifImage.format = AVIF_RGB_FORMAT_RGB;
        rgbAvifImage.ignoreAlpha = AVIF_TRUE;

        void* data = nullptr;
        if(swapChain.IsHDR())
        {
            size_t hdrsize = rgbAvifImage.width * rgbAvifImage.height * 3 * 2;
            data = malloc(hdrsize);
            uint16_t* dataview = (uint16_t*)data;
            {
                Vulkan::DeviceMemory* vkMemory = renderer_->GetScreenShotMemory();

                uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);

                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t idxDelta = extent.width * 4;
                uint32_t yy = 0;
                uint32_t xx = 0, idx = 0;
                for (uint32_t y = 0; y < extent.height; y++)
                {
                    xx = 0;
                    for (uint32_t x = 0; x < extent.width; x++)
                    {
                        uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                        uint32_t uInPixel = *pInPixel;
                        dataview[yy + xx + 2]     = (uInPixel & (0b1111111111 << 20)) >> 20;
                        dataview[yy + xx + 1] = (uInPixel & (0b1111111111 << 10)) >> 10;
                        dataview[yy + xx + 0] = (uInPixel & (0b1111111111 << 0)) >> 0;
                        
                        idx += 4;
                        xx += xDelta;
                    }
                    //idx += idxDelta;
                    yy += yDelta;
                }
                vkMemory->Unmap();
            }

            rgbAvifImage.pixels = (uint8_t*)data;
            rgbAvifImage.rowBytes = rgbAvifImage.width * 3 * sizeof(uint16_t);
        }
        else
        {
            data = malloc(imageSize);
            uint8_t* dataview = (uint8_t*)data;
            {
                Vulkan::DeviceMemory* vkMemory = renderer_->GetScreenShotMemory();

                uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, VK_WHOLE_SIZE);

                uint32_t yDelta = extent.width * kCompCnt;
                uint32_t xDelta = kCompCnt;
                uint32_t idxDelta = extent.width * 4;
                uint32_t yy = 0;
                uint32_t xx = 0, idx = 0;
                for (uint32_t y = 0; y < extent.height; y++)
                {
                    xx = 0;
                    for (uint32_t x = 0; x < extent.width; x++)
                    {
                        uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                        uint32_t uInPixel = *pInPixel;

                        dataview[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                        dataview[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                        dataview[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;

                        idx += 4;
                        xx += xDelta;
                    }
                    yy += yDelta;
                }
                vkMemory->Unmap();
            }

            rgbAvifImage.pixels = (uint8_t*)data;
            rgbAvifImage.rowBytes = rgbAvifImage.width * 3 * sizeof(uint8_t);
        }
       
       

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
        std::wfstream file(filename.c_str());
        file << avifOutput.data;
        file.close();
		
        free(data);

        // send to server
        img_encoded = base64_encode(avifOutput.data, avifOutput.size, false);
#else
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = renderer_->SwapChain();
        const auto extent = swapChain.Extent();

        // capture and export
        renderer_->CaptureScreenShot();

        constexpr uint32_t kCompCnt = 3;
        int imageSize = extent.width * extent.height * kCompCnt;

        uint8_t* data = (uint8_t*)malloc(imageSize);
        {
            Vulkan::DeviceMemory* vkMemory = renderer_->GetScreenShotMemory();

            uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, imageSize);

            uint32_t yDelta = extent.width * kCompCnt;
            uint32_t xDelta = kCompCnt;
            uint32_t idxDelta = extent.width * 4;
            uint32_t yy = 0;
            uint32_t xx = 0, idx = 0;
            for (uint32_t y = 0; y < extent.height; y++)
            {
                xx = 0;
                for (uint32_t x = 0; x < extent.width; x++)
                {
                    uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                    uint32_t uInPixel = *pInPixel;

                    data[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                    data[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                    data[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;

                    idx += 4;
                    xx += xDelta;
                }
                yy += yDelta;
            }
            vkMemory->Unmap();
        }

        // save to file with scenename
        std::string filename = sceneName + ".jpg";
        stbi_write_jpg(filename.c_str(), extent.width, extent.height, kCompCnt, (const void*)data, 91);
        fmt::print("screenshot saved to {}\n", filename);
        std::uintmax_t img_file_size = std::filesystem::file_size(filename);
        fmt::print("file size: {}\n", Utilities::metricFormatter(static_cast<double>(img_file_size), "b", 1024));
        // send to server
        //img_encoded = base64_encode(data, img_file_size, false);
        free(data);
#endif
    }

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
