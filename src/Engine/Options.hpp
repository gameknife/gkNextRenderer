#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace Runtime::Config
{

class Options final
{
public:

    class Help : public std::exception
    {
    public:

        Help() = default;
        ~Help() = default;
    };

    Options(int argc, const char* argv[]);
    ~Options() = default;

    // Application options.
    bool SaveFile{};
    bool RenderDoc{};
    bool ForceSDR{};
    bool ReferenceMode{};
    bool ForceNoRT{};
    bool ForceSoftGen{};
    bool HardwareQuery{};
    bool Validation{};
    bool SyncValidation{};
    bool FastExit{true};
    bool AgentValidation{};
    bool AgentVisibleWindow{};
#if GK_WITH_VITURE
    bool ArMode{};
    float ArWorldUnitsPerMeter{1.0f};
    float ArPredictionMs{20.0f};
    float ArSmoothingHz{0.0f};
    uint32_t ArDof{6};
#endif
    std::string AgentControl;
    std::string AgentControlToken;
    uint32_t ExitAfterFrames{};
    bool HiddenWindow{};
    // Force VK_EXT_headless_surface instead of creating an SDL window. Intended
    // for validating the headless render path on hosts with a desktop session.
    bool HeadlessSurface{};
    bool Tui{};
    uint32_t TuiFps{30};
    uint32_t TuiMaxCols{};
    uint32_t TuiMaxRows{};
    uint32_t TuiSsaa{1};
    std::string TuiCellMode{"half"};
    bool TuiNoInput{};
    bool DisableStreamline{};
    bool DisableFidelityFX{};
    bool RemoteMode{};
    bool RemoteShowWindow{};
    bool RemoteMultiView{};
    std::string RemoteBind{"0.0.0.0"};
    uint32_t RemoteHttpPort{8088};
    uint32_t RemotePort{8089};
    uint32_t RemoteBitrateKbps{};
    uint32_t RemoteFps{30};
    uint32_t RemoteWidth{};
    uint32_t RemoteHeight{};
    uint32_t RemoteMaxClients{2};
    std::string RemoteEncoder{"auto"};
    bool KeepCPUMeshData{};  // Retain CPU mesh data for editor workflows.
    bool HighPrecisionProgressiveHistory{}; // Use high-precision buffers for progressive accumulation/history.
    bool UpdateVisualTestBaseline{};
    bool FlappyReplay{};
    bool CppLiveCoding{true};
    bool ShaderHotReload{true};
    float ShaderHotReloadInterval{0.5f};
    std::vector<std::string> CVarOverrides{};
    std::string locale{};

    // Benchmark options used by gkNextMotionBenchmark.
    std::string BenchmarkConfig{};
    std::string AssetTrace{};
    // When set, write the reflection manifest to this path and exit without touching Vulkan.
    std::string DumpReflection{};

    
    // Scene options.
    std::string SceneName{};
    std::string HDRIfile{};
    // Vulkan options
    uint32_t GpuIdx{};
    // Select the Vulkan ICD before SDL loads the Vulkan loader.
    std::string VulkanDriver{"native"};

    // Window options
    uint32_t Width{};
    uint32_t Height{};
    // Preserve explicit CLI dimensions when headless validation substitutes its
    // smaller default framebuffer size.
    bool WidthSpecified{};
    bool HeightSpecified{};
    uint32_t PresentMode{};
    bool Fullscreen{};
    bool SystemDpiScaling{};

    // Test options
    bool TestGltfRobustness{};
    std::string TestGltfFilter{};
};

}

extern Runtime::Config::Options* GOption;
