#pragma once

#include <fstream>
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

struct FBenchmarkSettings final
{
    double warmupSeconds = 2.0;
    double durationSeconds = 10.0;
    std::string outputPath;
};

struct FBenchmarkGpuDrivenAverages final
{
    double drawCallsActual = 0.0;
    double drawCallsTotal = 0.0;
    double trisActual = 0.0;
    double trisTotal = 0.0;
};

class BenchMarker final
{
public:
    BenchMarker();
    explicit BenchMarker(FBenchmarkSettings settings);
    ~BenchMarker();
    
    void OnSceneStart( double nowInSeconds );
    bool OnTick( double nowInSeconds, Vulkan::VulkanBaseRenderer* renderer );
    void OnReport(Vulkan::VulkanBaseRenderer* renderer, const std::string& SceneName);
    void Report(Vulkan::VulkanBaseRenderer* renderer_, const std::string& sceneName, bool upload_screen, bool save_screen);
    // Benchmark stats
    FBenchmarkSettings settings_{};
    int32_t benchUnit_{};
    double time_{};
    double sceneInitialTime_{};
    double measurementInitialTime_{};
    double previousMeasurementTime_{};
    double periodInitialTime_{};
    uint32_t periodTotalFrames_{};
    uint32_t benchmarkTotalFrames_{};
    uint32_t gpuSampleCount_{};
    uint32_t gpuDrivenSampleCount_{};
    double frameTimeTotalMilliseconds_{};
    double gpuTimeTotalMilliseconds_{};
    double drawCallsActualTotal_{};
    double drawCallsTotal_{};
    double trisActualTotal_{};
    double trisTotal_{};
    bool measurementStarted_{};
    bool reportCompleted_{};
    std::ofstream benchmarkCsvReportFile;
};
