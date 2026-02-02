#include "BenchMark.hpp"
#include "Options.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Common/CoreMinimal.hpp"

#include "curl/curl.h"
#include "cpp-base64/base64.cpp"
#include "ThirdParty/json11/json11.hpp"
#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <filesystem>
#include <math.h>

#include "Runtime/Engine.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/Device.hpp"
#include "Runtime/ScreenShot.hpp"

//#include <spdlog/spdlog.h>

#if WITH_AVIF
#include "avif/avif.h"
#endif

BenchMarker::BenchMarker()
{
    std::time_t now = std::time(nullptr);
    std::string reportFilename = fmt::format("report_{:%d-%m-%Y-%H-%M-%S}.csv", *std::localtime(&now));

    benchmarkCsvReportFile.open(reportFilename);
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
            //SPDLOG_INFO("\t[Benchmarking] fps: {:.0f}", float(periodTotalFrames_) / float(totalTime));
            periodInitialTime_ = time_;
            periodTotalFrames_ = 0;
        }

        periodTotalFrames_++;
        benchmarkTotalFrames_++;
    }

    // If in benchmark mode, bail out from the scene if we've reached the time or sample limit.
    {
        const bool timeLimitReached = periodTotalFrames_ != 0 && time_ - sceneInitialTime_ > 10.0;
        if (timeLimitReached)
        {
            return true;
        }
    }
    return false;
}

void BenchMarker::OnReport(Vulkan::VulkanBaseRenderer* renderer, const std::string& sceneName)
{
    const double totalTime = time_ - sceneInitialTime_;
    
    double fps = benchmarkTotalFrames_ / totalTime;
    //SPDLOG_INFO("totalTime {:%H:%M:%S} fps {:.3f}", std::chrono::seconds(static_cast<long long>(totalTime)), fps);
    Report(renderer, static_cast<int>(floor(fps)), std::filesystem::path(sceneName).filename().replace_extension().string(), false, GOption->SaveFile);
}

inline const std::string VersionToString(const uint32_t version)
{
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

void BenchMarker::Report(Vulkan::VulkanBaseRenderer* renderer, int fps, const std::string& sceneName, bool uploadScreen, bool saveScreen)
{
    // report file
    benchmarkCsvReportFile << fmt::format("{},{},{}\n", benchUnit_++, sceneName, fps);
    benchmarkCsvReportFile.flush();
    // screenshot
    VkPhysicalDeviceProperties deviceProp1{};
    vkGetPhysicalDeviceProperties(renderer->Device().PhysicalDevice(), &deviceProp1);

    std::string imgEncoded {};
    if (uploadScreen || saveScreen)
    {
        ScreenShot::SaveSwapChainToFile(renderer, sceneName, 0, 0, 0, 0);
    }

    // perf server upload
    if( NextRenderer::GetBuildVersion() != "v0.0.0.0" )
    {
        json11::Json myJson = json11::Json::object{
            {"renderer", renderer->StaticClass()},
            {"scene", sceneName},
            {"gpu", std::string(deviceProp1.deviceName)},
            {"driver", VersionToString(deviceProp1.driverVersion)},
            {"fps", fps},
            {"version", NextRenderer::GetBuildVersion()},
            {"screenshot", imgEncoded}
        };
        std::string jsonStr = myJson.dump();

        //SPDLOG_INFO("Sending benchmark to perf server...");
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
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
        
            /* Perform the request, res gets the return code */
            res = curl_easy_perform(curl);
            /* Check for errors */
            //if (res != CURLE_OK)
                //SPDLOG_ERROR("curl_easy_perform() failed: {}", curl_easy_strerror(res));

            /* always cleanup */
            curl_easy_cleanup(curl);
        }
    }
}
