#include "BenchMark.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <nlohmann/json.hpp>
#include "cpp-base64/base64.cpp"
#include "curl/curl.h"
#include "stb_image_write.h"

using json = nlohmann::json;

#define _USE_MATH_DEFINES
#include <algorithm>
#include <filesystem>
#include <math.h>
#include <utility>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <imgui.h>

// #include <spdlog/spdlog.h>

#if WITH_AVIF
#include "avif/avif.h"
#endif

namespace
{
    constexpr double kBytesToMiB = 1.0 / (1024.0 * 1024.0);

    std::string MakeDefaultReportFilename()
    {
        std::time_t now = std::time(nullptr);
        return fmt::format("report_{:%d-%m-%Y-%H-%M-%S}.csv", *std::localtime(&now));
    }

    double DeviceLocalVramMiB(const Vulkan::MemoryStatsSnapshot& memoryStats)
    {
        const VkDeviceSize bytes = memoryStats.deviceLocalUsageBytes != 0
                                       ? memoryStats.deviceLocalUsageBytes
                                       : memoryStats.deviceLocalAllocationBytes;
        return static_cast<double>(bytes) * kBytesToMiB;
    }

    std::string GetPhysicalDeviceDriverName(VkPhysicalDevice physicalDevice)
    {
        if (physicalDevice == VK_NULL_HANDLE)
        {
            return {};
        }

        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

        VkPhysicalDeviceProperties2 deviceProperties{};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);

        return driverProperties.driverName[0] != '\0' ? std::string(driverProperties.driverName) : std::string{};
    }

    std::string DriverVersionToString(const VkPhysicalDeviceProperties& properties)
    {
        return to_string(Vulkan::Version(properties.driverVersion, properties.vendorID));
    }

    std::string GetPhysicalDeviceDriverInfo(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceProperties& properties)
    {
        const std::string driverName = GetPhysicalDeviceDriverName(physicalDevice);
        const std::string driverVersion = DriverVersionToString(properties);
        return driverName.empty() ? driverVersion : fmt::format("{} {}", driverName, driverVersion);
    }

    double SafeAverage(double total, uint32_t count)
    {
        return count > 0 ? total / static_cast<double>(count) : 0.0;
    }
}

void DrawBenchmarkStatsOverlay(NextEngine& engine)
{
    const Assets::Scene& scene = engine.GetScene();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->GetCenter().x, viewport->Pos.y + 12.0f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.72f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##BenchmarkStats", nullptr, flags))
    {
        ImGui::Text(
            "FPS %.0f  |  Nodes %zu  |  Models %zu  |  Materials %zu  |  Triangles %u",
            engine.GetFrameRate(),
            scene.Nodes().size(),
            scene.Models().size(),
            scene.Materials().size(),
            scene.GetTriangleCount());
    }
    ImGui::End();
}

BenchMarker::BenchMarker() : BenchMarker(FBenchmarkSettings{})
{
}

BenchMarker::BenchMarker(FBenchmarkSettings settings) : settings_(std::move(settings))
{
    const std::string reportFilename = settings_.outputPath.empty() ? MakeDefaultReportFilename() : settings_.outputPath;
    const std::filesystem::path reportPath(reportFilename);
    if (reportPath.has_parent_path())
    {
        std::filesystem::create_directories(reportPath.parent_path());
    }

    benchmarkCsvReportFile.open(reportFilename);
    benchmarkCsvReportFile << fmt::format(
        "#,scene,renderer,gpu,driver,resolution,frame_time_ms,gpu_time_ms,fps,vram_mib,draw_calls_actual,draw_calls_total,tris_actual,tris_total,frames,duration_s,upscaler_type,super_resolution\n");
}

BenchMarker::~BenchMarker() { benchmarkCsvReportFile.close(); }

void BenchMarker::OnSceneStart(double nowInSeconds)
{
    periodTotalFrames_ = 0;
    benchmarkTotalFrames_ = 0;
    gpuSampleCount_ = 0;
    gpuDrivenSampleCount_ = 0;
    frameTimeTotalMilliseconds_ = 0.0;
    gpuTimeTotalMilliseconds_ = 0.0;
    drawCallsActualTotal_ = 0.0;
    drawCallsTotal_ = 0.0;
    trisActualTotal_ = 0.0;
    trisTotal_ = 0.0;
    sceneInitialTime_ = nowInSeconds;
    measurementInitialTime_ = 0.0;
    previousMeasurementTime_ = 0.0;
    measurementStarted_ = false;
    reportCompleted_ = false;
}

bool BenchMarker::OnTick(double nowInSeconds, Vulkan::VulkanBaseRenderer* renderer)
{
    if (reportCompleted_)
    {
        return false;
    }

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

        if (periodTotalFrames_ != 0 &&
            static_cast<uint64_t>(prevTotalTime / period) != static_cast<uint64_t>(totalTime / period))
        {
            // SPDLOG_INFO("\t[Benchmarking] fps: {:.0f}", float(periodTotalFrames_) / float(totalTime));
            periodInitialTime_ = time_;
            periodTotalFrames_ = 0;
        }

        periodTotalFrames_++;
    }

    const double sceneElapsedSeconds = time_ - sceneInitialTime_;
    if (sceneElapsedSeconds < settings_.warmupSeconds)
    {
        return false;
    }

    if (!measurementStarted_)
    {
        measurementStarted_ = true;
        measurementInitialTime_ = time_;
        previousMeasurementTime_ = time_;
        periodInitialTime_ = time_;
        periodTotalFrames_ = 0;
        benchmarkTotalFrames_ = 0;
        return false;
    }

    const double frameSeconds = std::max(0.0, time_ - previousMeasurementTime_);
    previousMeasurementTime_ = time_;
    frameTimeTotalMilliseconds_ += frameSeconds * 1000.0;
    benchmarkTotalFrames_++;

    if (renderer != nullptr && renderer->Profiler() != nullptr)
    {
        const float gpuMilliseconds = renderer->Profiler()->GetGpuTime("[gpu]");
        if (gpuMilliseconds > 0.0f)
        {
            gpuTimeTotalMilliseconds_ += gpuMilliseconds;
            gpuSampleCount_++;
        }
    }
    if (renderer != nullptr)
    {
        const Assets::GPUDrivenStat& stat = renderer->GetScene().GetGpuDrivenStat();
        const uint32_t drawCallsActual = stat.ProcessedCount > stat.CulledCount
                                             ? stat.ProcessedCount - stat.CulledCount
                                             : 0u;
        const uint32_t trisActual = stat.TriangleCount > stat.CulledTriangleCount
                                        ? stat.TriangleCount - stat.CulledTriangleCount
                                        : 0u;
        drawCallsActualTotal_ += static_cast<double>(drawCallsActual);
        drawCallsTotal_ += static_cast<double>(stat.ProcessedCount);
        trisActualTotal_ += static_cast<double>(trisActual);
        trisTotal_ += static_cast<double>(stat.TriangleCount);
        gpuDrivenSampleCount_++;
    }

    return time_ - measurementInitialTime_ >= settings_.durationSeconds;
}

void BenchMarker::OnReport(Vulkan::VulkanBaseRenderer* renderer, const std::string& sceneName)
{
    reportCompleted_ = true;
    Report(renderer, std::filesystem::path(sceneName).filename().replace_extension().string(), false, GOption->SaveFile);
}

void BenchMarker::Report(Vulkan::VulkanBaseRenderer* renderer, const std::string& sceneName, bool uploadScreen,
                         bool saveScreen)
{
    const double totalTime = std::max(0.000001, time_ - measurementInitialTime_);
    const double fps = static_cast<double>(benchmarkTotalFrames_) / totalTime;
    const double frameTimeMilliseconds = benchmarkTotalFrames_ > 0
                                             ? frameTimeTotalMilliseconds_ / static_cast<double>(benchmarkTotalFrames_)
                                             : 0.0;
    const double gpuTimeMilliseconds = gpuSampleCount_ > 0
                                           ? gpuTimeTotalMilliseconds_ / static_cast<double>(gpuSampleCount_)
                                           : 0.0;
    const double drawCallsActual = SafeAverage(drawCallsActualTotal_, gpuDrivenSampleCount_);
    const double drawCallsTotal = SafeAverage(drawCallsTotal_, gpuDrivenSampleCount_);
    const double trisActual = SafeAverage(trisActualTotal_, gpuDrivenSampleCount_);
    const double trisTotal = SafeAverage(trisTotal_, gpuDrivenSampleCount_);
    const Runtime::Config::UserSettings& userSettings = NextEngine::GetInstance()->GetUserSettings();
    const Vulkan::MemoryStatsSnapshot memoryStats = renderer->Device().CaptureMemoryStats(false);
    const double vramMiB = DeviceLocalVramMiB(memoryStats);
    const std::string resolution = fmt::format("{}x{}", GOption->Width, GOption->Height);
    const std::string rendererName = Vulkan::GetRendererName(renderer->CurrentLogicRendererType());

    // report file
    VkPhysicalDeviceProperties deviceProp1{};
    vkGetPhysicalDeviceProperties(renderer->Device().PhysicalDevice(), &deviceProp1);
    const std::string driverInfo = GetPhysicalDeviceDriverInfo(renderer->Device().PhysicalDevice(), deviceProp1);

    benchmarkCsvReportFile << fmt::format("{},{},{},{},{},{},{:.3f},{:.3f},{:.2f},{:.1f},{:.2f},{:.2f},{:.2f},{:.2f},{},{:.3f},{},{}\n",
                                          benchUnit_++,
                                          sceneName,
                                          rendererName,
                                          deviceProp1.deviceName,
                                          driverInfo,
                                          resolution,
                                          frameTimeMilliseconds,
                                          gpuTimeMilliseconds,
                                          fps,
                                          vramMiB,
                                          drawCallsActual,
                                          drawCallsTotal,
                                          trisActual,
                                          trisTotal,
                                          benchmarkTotalFrames_,
                                          totalTime,
                                          userSettings.UpscalerType,
                                          userSettings.SuperResolution);
    benchmarkCsvReportFile.flush();

    SPDLOG_INFO("[Benchmark] scene={} renderer={} gpu={} driver={} frame={:.3f}ms gpu={:.3f}ms fps={:.2f} vram={:.1f}MiB draw={:.2f}/{:.2f} tris={:.2f}/{:.2f}",
                sceneName, rendererName, deviceProp1.deviceName, driverInfo, frameTimeMilliseconds,
                gpuTimeMilliseconds, fps, vramMiB, drawCallsActual, drawCallsTotal, trisActual, trisTotal);

    std::string imgEncoded{};
    if (uploadScreen || saveScreen)
    {
        NextEngine::GetInstance()->RequestScreenShot({.filename = sceneName});
    }

    // perf server upload
    if (NextRenderer::GetBuildVersion() != "v0.0.0.0")
    {
        json myJson = json{{"renderer", renderer->StaticClass()},
                           {"scene", sceneName},
                           {"gpu", std::string(deviceProp1.deviceName)},
                           {"driver", driverInfo},
                           {"fps", fps},
                           {"frame_time_ms", frameTimeMilliseconds},
                           {"gpu_time_ms", gpuTimeMilliseconds},
                           {"vram_mib", vramMiB},
                           {"draw_calls_actual", drawCallsActual},
                           {"draw_calls_total", drawCallsTotal},
                           {"tris_actual", trisActual},
                           {"tris_total", trisTotal},
                           {"version", NextRenderer::GetBuildVersion()},
                           {"screenshot", imgEncoded}};
        std::string jsonStr = myJson.dump();

        // SPDLOG_INFO("Sending benchmark to perf server...");
        //  upload from curl
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
            // if (res != CURLE_OK)
            // SPDLOG_ERROR("curl_easy_perform() failed: {}", curl_easy_strerror(res));

            /* always cleanup */
            curl_easy_cleanup(curl);
        }
    }
}
