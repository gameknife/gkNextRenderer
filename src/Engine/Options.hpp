#pragma once

#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Runtime::Config
{

enum class ERenderCapacityMode : uint8_t
{
    Default,
    Massive,
};

struct FRenderCapacityLimits
{
    static constexpr uint32_t defaultRenderProxyCapacity = 65535;
    static constexpr uint32_t defaultVisibilityProxyCapacity = 32767;
    static constexpr uint32_t massiveRenderProxyCapacity = defaultRenderProxyCapacity * 4;

    ERenderCapacityMode mode = ERenderCapacityMode::Default;
    uint32_t renderProxyCapacity = defaultRenderProxyCapacity;
    uint32_t visibilityProxyCapacity = defaultVisibilityProxyCapacity;
    uint32_t primitiveWordCount = 1;

    static constexpr FRenderCapacityLimits FromMode(ERenderCapacityMode value)
    {
        return value == ERenderCapacityMode::Massive
            ? FRenderCapacityLimits{value, massiveRenderProxyCapacity, massiveRenderProxyCapacity, 2}
            : FRenderCapacityLimits{};
    }

    constexpr bool IsMassive() const { return mode == ERenderCapacityMode::Massive; }
    constexpr bool IsValidOneBasedProxySlot(uint32_t slot) const
    {
        return slot != 0 && slot <= visibilityProxyCapacity;
    }

    static constexpr std::optional<uint64_t> CheckedByteSize(
        uint64_t elementCount, uint64_t elementSize, uint64_t slotCount = 1)
    {
        if (elementSize != 0 && elementCount > std::numeric_limits<uint64_t>::max() / elementSize)
        {
            return std::nullopt;
        }
        const uint64_t bytes = elementCount * elementSize;
        if (slotCount != 0 && bytes > std::numeric_limits<uint64_t>::max() / slotCount)
        {
            return std::nullopt;
        }
        return bytes * slotCount;
    }
};

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
    std::string AgentControl;
    std::string AgentControlToken;
    bool HiddenWindow{};
    bool Tui{};
    uint32_t TuiFps{30};
    uint32_t TuiMaxCols{};
    uint32_t TuiMaxRows{};
    uint32_t TuiSsaa{1};
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
    ERenderCapacityMode RenderCapacityMode{ERenderCapacityMode::Default};
    bool UpdateVisualTestBaseline{};
    bool FlappyReplay{};
    bool CppLiveCoding{true};
    bool ShaderHotReload{true};
    float ShaderHotReloadInterval{0.5f};
    std::vector<std::string> CVarOverrides{};
    std::string locale{};

    // Benchmark options used by gkNextMotionBenchmark.
    std::string BenchmarkConfig{};

    
    // Scene options.
    std::string SceneName{};
    std::string HDRIfile{};
    // Vulkan options
    uint32_t GpuIdx{};

    // Window options
    uint32_t Width{};
    uint32_t Height{};
    uint32_t PresentMode{};
    bool Fullscreen{};
    bool SystemDpiScaling{};

    // Test options
    bool TestGltfRobustness{};
    std::string TestGltfFilter{};
};

}

extern Runtime::Config::Options* GOption;
