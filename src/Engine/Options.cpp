#include "Engine/Options.hpp"
#include "Engine/Utilities/Exception.hpp"
#include <cxxopts.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace Runtime::Config
{

namespace
{
    // Groups shown by --help. Automation options exist for gnb and CI drivers and only
    // appear with --help-all, so the default usage text stays readable for end users.
    const std::vector<std::string> UserFacingGroups = {"", "Display", "Rendering", "Remote Play", "Terminal", "Diagnostics"};
    const std::vector<std::string> AllGroups = {"", "Display", "Rendering", "Remote Play", "Terminal", "Diagnostics", "Automation"};
}

Options::Options(const int argc, const char* argv[])
{
    const bool disableStreamlineForApplication =
        argc > 0 && argv[0] != nullptr &&
        std::filesystem::path(argv[0]).stem().string() == "gkNextEditor";

    cxxopts::Options options("gkNextRenderer", "A cross-platform Vulkan renderer and engine.");
    std::string remoteResolution;

    options.add_options()
        ("load-scene", "Scene to load. Absolute path, or relative to the application directory.", cxxopts::value<std::string>(SceneName)->default_value(""))
        ("hdri", "HDR environment map to load.", cxxopts::value<std::string>(HDRIfile)->default_value(""))
        ("locale", "User interface language: en or zhCN. The interface is predominantly English; zhCN covers a subset.", cxxopts::value<std::string>(locale)->default_value("en"))
        ("h,help", "Print the common options.")
        ("help-all", "Print every option, including automation and test hooks.");

    options.add_options("Display")
        ("width", "Framebuffer width in pixels.", cxxopts::value<uint32_t>(Width)->default_value("1920"))
        ("height", "Framebuffer height in pixels.", cxxopts::value<uint32_t>(Height)->default_value("1080"))
        ("fullscreen", "Start borderless fullscreen instead of windowed.", cxxopts::value<bool>(Fullscreen)->default_value("false"))
#if GK_WITH_VITURE
        ("ar", "Enable VITURE AR head tracking.", cxxopts::value<bool>(ArMode)->default_value("false")->implicit_value("true"))
        ("ar-world-units-per-meter", "World-unit scale applied to tracked physical translation.", cxxopts::value<float>(ArWorldUnitsPerMeter)->default_value("1.0"))
        ("ar-prediction-ms", "VITURE pose prediction horizon in milliseconds (default: 20.0).", cxxopts::value<float>(ArPredictionMs)->default_value("20.0"))
        ("ar-smoothing-hz", "AR pose low-pass smoothing frequency in Hz (0 disables smoothing; default: 0).", cxxopts::value<float>(ArSmoothingHz)->default_value("0.0"))
        ("ar-dof", "VITURE Carina tracking mode: 3 or 6 (default: 6).", cxxopts::value<uint32_t>(ArDof)->default_value("6"))
#endif
        ("present-mode", "Presentation mode (0 = Immediate, 1 = MailBox, 2 = FIFO, 3 = FIFO relaxed).", cxxopts::value<uint32_t>(PresentMode)->default_value("2"))
        ("forcesdr", "Force SDR output even when the display supports HDR.", cxxopts::value<bool>(ForceSDR)->default_value("false"))
        ("system-dpi-scaling", "Use legacy Windows bitmap DPI scaling instead of engine-native DPI scaling.",
         cxxopts::value<bool>(SystemDpiScaling)->default_value("false")->implicit_value("true"));

    options.add_options("Rendering")
        ("gpu", "Index of the GPU to render on. Use --hwquery to list capabilities.", cxxopts::value<uint32_t>(GpuIdx)->default_value("0"))
        ("forcenort", "Disable hardware ray tracing and fall back to the software renderers.", cxxopts::value<bool>(ForceNoRT)->default_value("false"))
        ("forcesoftgen", "Force software ray tracing for ambient cube generation.", cxxopts::value<bool>(ForceSoftGen)->default_value("false"))
        ("disable-streamline", "Disable NVIDIA Streamline (DLSS, Reflex, Frame Generation).", cxxopts::value<bool>(DisableStreamline)->default_value("false")->implicit_value("true"))
        ("disable-fidelityfx", "Disable AMD FidelityFX (FSR).", cxxopts::value<bool>(DisableFidelityFX)->default_value("false")->implicit_value("true"))
        ("cvar", "Apply a startup CVar override, e.g. --cvar \"r.upscaler.type 1\". Repeatable.", cxxopts::value<std::vector<std::string>>(CVarOverrides))
        ("shader-hotreload", "Enable Slang shader hot reload.", cxxopts::value<bool>(ShaderHotReload)->default_value("true")->implicit_value("true"))
        ("no-shader-hotreload", "Disable Slang shader hot reload.", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("shader-hotreload-interval", "Shader hot reload poll interval in seconds.", cxxopts::value<float>(ShaderHotReloadInterval)->default_value("0.5"))
        ("reference", "Reference renderer comparison mode.", cxxopts::value<bool>(ReferenceMode)->default_value("false"))
        ("savefile", "Save a screenshot after each benchmark run.", cxxopts::value<bool>(SaveFile)->default_value("false"));

    options.add_options("Remote Play")
        ("remote", "Enable WebRTC Remote Play host mode. Implies --hidden-window and --forcesdr unless --remote-show-window is set.", cxxopts::value<bool>(RemoteMode)->default_value("false")->implicit_value("true"))
        ("remote-show-window", "Keep the desktop window visible while --remote is active.", cxxopts::value<bool>(RemoteShowWindow)->default_value("false")->implicit_value("true"))
        ("remote-multiview", "Give each browser client its own camera and input context. Requires --remote.", cxxopts::value<bool>(RemoteMultiView)->default_value("false")->implicit_value("true"))
        ("remote-bind", "Bind address.", cxxopts::value<std::string>(RemoteBind)->default_value("0.0.0.0"))
        ("remote-http-port", "HTTP port serving the browser client.", cxxopts::value<uint32_t>(RemoteHttpPort)->default_value("8088"))
        ("remote-port", "Signaling WebSocket port.", cxxopts::value<uint32_t>(RemotePort)->default_value("8089"))
        ("remote-bitrate", "Starting video bitrate in kbps. 0 = automatic.", cxxopts::value<uint32_t>(RemoteBitrateKbps)->default_value("0"))
        ("remote-fps", "Target stream frame rate.", cxxopts::value<uint32_t>(RemoteFps)->default_value("60"))
        ("remote-res", "Encode resolution, e.g. 1280x720. Empty means the source resolution.", cxxopts::value<std::string>(remoteResolution)->default_value(""))
        ("remote-max-clients", "Maximum simultaneous clients in --remote-multiview mode.", cxxopts::value<uint32_t>(RemoteMaxClients)->default_value("2"))
        ("remote-encoder", "Video encoder: auto or vulkan.", cxxopts::value<std::string>(RemoteEncoder)->default_value("auto"));

    options.add_options("Terminal")
        ("tui", "Render into the current terminal using truecolor block characters.", cxxopts::value<bool>(Tui)->default_value("false")->implicit_value("true"))
        ("tui-fps", "Maximum terminal refresh rate.", cxxopts::value<uint32_t>(TuiFps)->default_value("30"))
        ("tui-max-cols", "Column cap (0 = auto).", cxxopts::value<uint32_t>(TuiMaxCols)->default_value("0"))
        ("tui-max-rows", "Row cap (0 = auto).", cxxopts::value<uint32_t>(TuiMaxRows)->default_value("0"))
        ("tui-ssaa", "Supersample factor for hidden rendering (1-16).", cxxopts::value<uint32_t>(TuiSsaa)->default_value("4"))
        ("tui-cell-mode", "Character layout: half or quadrant.", cxxopts::value<std::string>(TuiCellMode)->default_value("half"))
        ("tui-no-input", "Do not capture stdin.", cxxopts::value<bool>(TuiNoInput)->default_value("false")->implicit_value("true"));

    options.add_options("Diagnostics")
        ("hwquery", "Log the selected device's ray tracing and feature support at startup.", cxxopts::value<bool>(HardwareQuery)->default_value("true"))
        ("validation", "Enable Vulkan validation layers.", cxxopts::value<bool>(Validation)->default_value("false"))
        ("sync-validation", "Enable Vulkan synchronization validation. Implies --validation.", cxxopts::value<bool>(SyncValidation)->default_value("false")->implicit_value("true"))
        ("vulkan-driver", "Vulkan driver selection: native, lvp (Windows/Linux), or dozen (Windows).", cxxopts::value<std::string>(VulkanDriver)->default_value("native"))
        ("renderdoc", "Capture the first ready scene frame and open it in RenderDoc if available.", cxxopts::value<bool>(RenderDoc)->default_value("false")->implicit_value("true"))
        ("hidden-window", "Create the window hidden: no popup and no focus steal.", cxxopts::value<bool>(HiddenWindow)->default_value("false")->implicit_value("true"))
        ("headless-surface", "Use VK_EXT_headless_surface and skip SDL window creation. Intended for headless-path testing.", cxxopts::value<bool>(HeadlessSurface)->default_value("false")->implicit_value("true"))
        ("benchmark-config", "Load a gkNextMotionBenchmark orchestration JSON.", cxxopts::value<std::string>(BenchmarkConfig)->default_value(""));

    options.add_options("Automation")
        ("agent-validation", "Agent validation mode: hidden window, immediate present, deterministic pacing. Driven by gnb (see `gnb shot` / `gnb validate`).", cxxopts::value<bool>(AgentValidation)->default_value("false")->implicit_value("true"))
        ("agent-visible-window", "Keep the window visible during agent validation.", cxxopts::value<bool>(AgentVisibleWindow)->default_value("false")->implicit_value("true"))
        ("agent-control", "Loopback endpoint for gnb runtime control (host:port).", cxxopts::value<std::string>(AgentControl)->default_value(""))
        ("agent-control-token", "One-time token for gnb runtime control.", cxxopts::value<std::string>(AgentControlToken)->default_value(""))
        ("asset-trace", "Append successfully resolved runtime asset paths to this manifest.", cxxopts::value<std::string>(AssetTrace)->default_value(""))
        ("dump-reflection", "Write the entt::meta reflection manifest to this path and exit. Source for the generated C# component wrappers; see `gnb csharpgen --refresh`.", cxxopts::value<std::string>(DumpReflection)->default_value(""))
        ("fastexit", "Skip task teardown on exit.", cxxopts::value<bool>(FastExit)->default_value("true"))
        ("update-baseline", "Update visual test baseline images from the current run.", cxxopts::value<bool>(UpdateVisualTestBaseline)->default_value("false")->implicit_value("true"))
        ("flappy-replay", "Run the Flappy deterministic replay and write trace output.", cxxopts::value<bool>(FlappyReplay)->default_value("false")->implicit_value("true"))
        ("cpp-live-coding", "Enable Live++ C++ live coding when compiled in.", cxxopts::value<bool>(CppLiveCoding)->default_value("true")->implicit_value("true"))
        ("no-cpp-live-coding", "Disable Live++ C++ live coding.", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("test-gltf", "Run the glTF robustness test from the Khronos Sample Assets (test runner only).", cxxopts::value<bool>(TestGltfRobustness)->default_value("false"))
        ("test-gltf-filter", "Filter for the glTF robustness test (partial name match).", cxxopts::value<std::string>(TestGltfFilter)->default_value(""));

    try
    {
        auto result = options.parse(argc, argv);

        WidthSpecified = result.count("width") != 0;
        HeightSpecified = result.count("height") != 0;

        if (result.count("help-all"))
        {
            std::cout << options.help(AllGroups) << std::endl;
            exit(0);
        }
        if (result.count("help"))
        {
            std::cout << options.help(UserFacingGroups) << std::endl;
            std::cout << "Run with --help-all to also list automation and test options." << std::endl;
            exit(0);
        }

        if (SyncValidation)
        {
            Validation = true;
        }
        DisableStreamline = DisableStreamline || disableStreamlineForApplication || AgentValidation;
        DisableFidelityFX = DisableFidelityFX || AgentValidation;

        if (VulkanDriver != "native" && VulkanDriver != "lvp" && VulkanDriver != "dozen")
        {
            Throw(std::out_of_range("Invalid --vulkan-driver. Expected native, lvp, or dozen."));
        }
        if (VulkanDriver == "lvp" || VulkanDriver == "dozen")
        {
            // These integrations expect a native vendor driver and their device
            // augmenters are not part of the software-ICD path.
            DisableStreamline = true;
            DisableFidelityFX = true;
        }

        if (result["no-shader-hotreload"].as<bool>())
        {
            ShaderHotReload = false;
        }

        if (result["no-cpp-live-coding"].as<bool>() || AgentValidation)
        {
            CppLiveCoding = false;
        }

        if (ShaderHotReloadInterval < 0.1f)
        {
            ShaderHotReloadInterval = 0.1f;
        }

        if (!remoteResolution.empty())
        {
            const size_t separator = remoteResolution.find('x');
            if (separator == std::string::npos || separator == 0 || separator + 1 >= remoteResolution.size())
            {
                Throw(std::out_of_range("Invalid --remote-res. Expected WIDTHxHEIGHT."));
            }
            try
            {
                RemoteWidth = static_cast<uint32_t>(std::stoul(remoteResolution.substr(0, separator)));
                RemoteHeight = static_cast<uint32_t>(std::stoul(remoteResolution.substr(separator + 1)));
            }
            catch (const std::exception&)
            {
                Throw(std::out_of_range("Invalid --remote-res. Expected WIDTHxHEIGHT."));
            }
        }

        if (RemoteMode)
        {
            ForceSDR = true;
            if (!RemoteShowWindow)
            {
                HiddenWindow = true;
            }
            if (RemoteFps == 0)
            {
                RemoteFps = 60;
            }
            if (RemoteHttpPort > 65535 || RemotePort > 65535)
            {
                Throw(std::out_of_range("Remote Play ports must be in range 0..65535."));
            }
            if (RemoteEncoder != "auto" && RemoteEncoder != "vulkan")
            {
                Throw(std::out_of_range("Invalid --remote-encoder. Expected auto or vulkan."));
            }
            if (RemoteMaxClients == 0)
            {
                Throw(std::out_of_range("Remote Play --remote-max-clients must be at least 1."));
            }
        }
        else if (RemoteMultiView)
        {
            Throw(std::out_of_range("--remote-multiview requires --remote."));
        }

        if (Tui)
        {
            ForceSDR = true;
            HiddenWindow = true;
            if (TuiFps == 0)
            {
                TuiFps = 30;
            }
            TuiSsaa = std::clamp(TuiSsaa, 1u, 16u);
            if (TuiCellMode == "half-block")
            {
                TuiCellMode = "half";
            }
            if (TuiCellMode != "half" && TuiCellMode != "quadrant")
            {
                Throw(std::out_of_range("Invalid --tui-cell-mode. Expected half or quadrant."));
            }
        }

        if (PresentMode > 3)
        {
            Throw(std::out_of_range("Invalid present mode."));
        }

#if GK_WITH_VITURE
        if (!std::isfinite(ArPredictionMs) || ArPredictionMs < 0.0f || ArPredictionMs > 100.0f)
        {
            Throw(std::out_of_range("Invalid --ar-prediction-ms. Expected a value from 0 to 100."));
        }
        if (!std::isfinite(ArSmoothingHz) || ArSmoothingHz < 0.0f || ArSmoothingHz > 240.0f)
        {
            Throw(std::out_of_range("Invalid --ar-smoothing-hz. Expected a value from 0 to 240."));
        }
        if (ArDof != 3 && ArDof != 6)
        {
            Throw(std::out_of_range("Invalid --ar-dof. Expected 3 or 6."));
        }
#endif
    }
    catch (const cxxopts::exceptions::exception& e)
    {
        // A bad command line is a failure: exiting 0 made scripts and CI treat a typo
        // as a successful run.
        std::cerr << e.what() << "\n\nRun with --help for the available options." << std::endl;
        exit(2);
    }
}

}
