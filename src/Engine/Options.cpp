#include "Engine/Options.hpp"
#include "Engine/Utilities/Exception.hpp"
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>

namespace Runtime::Config
{

Options::Options(const int argc, const char* argv[])
{
	const bool disableStreamlineForApplication =
		argc > 0 && argv[0] != nullptr &&
		std::filesystem::path(argv[0]).stem().string() == "gkNextEditor";

	cxxopts::Options options("options", "");
	std::string remoteResolution;
	options.add_options()
		("load-scene", "The scene to load. absolute path or relative path to project root.", cxxopts::value<std::string>(SceneName)->default_value(""))
		("hdri", "The HDRI file to load.", cxxopts::value<std::string>(HDRIfile)->default_value(""))

		("gpu", "Explicitly set the usage gpu idx.", cxxopts::value<uint32_t>(GpuIdx)->default_value("0"))

		("width", "The framebuffer width.", cxxopts::value<uint32_t>(Width)->default_value("1920"))
		("height", "The framebuffer height.", cxxopts::value<uint32_t>(Height)->default_value("1080"))
		("present-mode", "The present mode (0 = Immediate, 1 = MailBox, 2 = FIFO, 3 = FIFORelaxed).", cxxopts::value<uint32_t>(PresentMode)->default_value("3"))
		("fullscreen", "Toggle fullscreen vs windowed (default: windowed).", cxxopts::value<bool>(Fullscreen)->default_value("false"))

		("savefile", "Save screenshot every benchmark finish.", cxxopts::value<bool>(SaveFile)->default_value("false"))
		("renderdoc", "Attach renderdoc if available.", cxxopts::value<bool>(RenderDoc)->default_value("false"))
		("forcesdr", "Force use SDR Display even supported.", cxxopts::value<bool>(ForceSDR)->default_value("false"))
		("locale", "Locale: en, zhCN, RU.", cxxopts::value<std::string>(locale)->default_value("en"))
		("reference", "Reference Renderer Compare Mode.", cxxopts::value<bool>(ReferenceMode)->default_value("false"))
		("forcenort", "Forcing hardware raytracing not supported.", cxxopts::value<bool>(ForceNoRT)->default_value("false"))
		("forcesoftgen", "Forcing software raytracing for ambient cube gen.", cxxopts::value<bool>(ForceSoftGen)->default_value("false"))
		
		("hwquery", "Forcing hardware raytracing not supported.", cxxopts::value<bool>(HardwareQuery)->default_value("true"))
		("validation", "Force enable validation layers.", cxxopts::value<bool>(Validation)->default_value("false"))
		("sync-validation", "Enable Vulkan synchronization validation. Implies --validation.", cxxopts::value<bool>(SyncValidation)->default_value("false")->implicit_value("true"))
		("fastexit", "Enable fast exit by skipping task wait.", cxxopts::value<bool>(FastExit)->default_value("true"))
		("agent-validation", "Agent validation mode: hidden window, immediate present, deterministic pacing. Driven by gnb via --agent-control (see `gnb shot` / `gnb validate`).", cxxopts::value<bool>(AgentValidation)->default_value("false")->implicit_value("true"))
		("agent-visible-window", "Keep the desktop window visible while running agent validation or an agent script.", cxxopts::value<bool>(AgentVisibleWindow)->default_value("false")->implicit_value("true"))
		("agent-control", "Loopback endpoint for gnb runtime control (host:port).", cxxopts::value<std::string>(AgentControl)->default_value(""))
		("agent-control-token", "One-time token for gnb runtime control.", cxxopts::value<std::string>(AgentControlToken)->default_value(""))
		("hidden-window", "Create the window hidden (no focus steal / no popup). Implied by --agent-validation; useful for unit tests.", cxxopts::value<bool>(HiddenWindow)->default_value("false")->implicit_value("true"))
		("tui", "Render the hidden swapchain into the current terminal using truecolor block characters.", cxxopts::value<bool>(Tui)->default_value("false")->implicit_value("true"))
		("tui-fps", "Maximum terminal refresh rate for --tui.", cxxopts::value<uint32_t>(TuiFps)->default_value("30"))
		("tui-max-cols", "Optional column cap for --tui (0 = auto).", cxxopts::value<uint32_t>(TuiMaxCols)->default_value("0"))
		("tui-max-rows", "Optional row cap for --tui (0 = auto).", cxxopts::value<uint32_t>(TuiMaxRows)->default_value("0"))
		("tui-ssaa", "Supersample factor for --tui hidden rendering (1-4).", cxxopts::value<uint32_t>(TuiSsaa)->default_value("1"))
		("tui-no-input", "Do not capture stdin in --tui mode.", cxxopts::value<bool>(TuiNoInput)->default_value("false")->implicit_value("true"))
		("disable-streamline", "Disable NVIDIA Streamline/DLSS integration for this process.", cxxopts::value<bool>(DisableStreamline)->default_value("false")->implicit_value("true"))
		("remote", "Enable WebRTC Remote Play host mode. Implies --hidden-window and --forcesdr unless --remote-show-window is set.", cxxopts::value<bool>(RemoteMode)->default_value("false")->implicit_value("true"))
		("remote-show-window", "Keep the desktop window visible while --remote is active.", cxxopts::value<bool>(RemoteShowWindow)->default_value("false")->implicit_value("true"))
		("remote-multiview", "Enable Cloud Play multi-client multi-view mode. Each browser owns an independent remote camera/input context.", cxxopts::value<bool>(RemoteMultiView)->default_value("false")->implicit_value("true"))
		("remote-bind", "Remote Play bind address.", cxxopts::value<std::string>(RemoteBind)->default_value("0.0.0.0"))
		("remote-http-port", "Remote Play HTTP client port.", cxxopts::value<uint32_t>(RemoteHttpPort)->default_value("8088"))
		("remote-port", "Remote Play signaling WebSocket port.", cxxopts::value<uint32_t>(RemotePort)->default_value("8089"))
		("remote-bitrate", "Remote Play starting video bitrate in kbps. 0 = auto.", cxxopts::value<uint32_t>(RemoteBitrateKbps)->default_value("0"))
		("remote-fps", "Remote Play target stream frame rate.", cxxopts::value<uint32_t>(RemoteFps)->default_value("60"))
		("remote-res", "Remote Play encode resolution, e.g. 1280x720. Empty means source resolution.", cxxopts::value<std::string>(remoteResolution)->default_value(""))
		("remote-max-clients", "Maximum simultaneous Remote Play clients in --remote-multiview mode.", cxxopts::value<uint32_t>(RemoteMaxClients)->default_value("2"))
		("remote-encoder", "Remote Play video encoder: auto or vulkan.", cxxopts::value<std::string>(RemoteEncoder)->default_value("auto"))
		("keep-cpu-mesh-data", "Keep CPU mesh data for editor mode.", cxxopts::value<bool>(KeepCPUMeshData)->default_value("false"))
		("update-baseline", "Update visual test baseline images from the current run.", cxxopts::value<bool>(UpdateVisualTestBaseline)->default_value("false")->implicit_value("true"))
		("flappy-replay", "Run Flappy deterministic replay and write trace output.", cxxopts::value<bool>(FlappyReplay)->default_value("false")->implicit_value("true"))
		("shader-hotreload", "Enable Slang shader hot reload.", cxxopts::value<bool>(ShaderHotReload)->default_value("true")->implicit_value("true"))
		("no-shader-hotreload", "Disable Slang shader hot reload.", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
		("shader-hotreload-interval", "Slang shader hot reload poll interval in seconds.", cxxopts::value<float>(ShaderHotReloadInterval)->default_value("0.5"))
		("cvar", "Apply a startup CVar override, e.g. --cvar \"r.dlss 1\". Can be repeated.", cxxopts::value<std::vector<std::string>>(CVarOverrides))
		("benchmark-config", "Load gkNextMotionBenchmark orchestration JSON.", cxxopts::value<std::string>(BenchmarkConfig)->default_value(""))

		("test-gltf", "Run glTF robustness test from Khronos Sample Assets.", cxxopts::value<bool>(TestGltfRobustness)->default_value("false"))
		("test-gltf-filter", "Filter for glTF robustness test (partial name match).", cxxopts::value<std::string>(TestGltfFilter)->default_value(""))

		("h,help", "Print usage");
	try
	{
		auto result = options.parse(argc, argv);
		if (SyncValidation)
		{
			Validation = true;
		}
		DisableStreamline = DisableStreamline || disableStreamlineForApplication || AgentValidation;

		if (result.count("help"))
		{
			std::cout << options.help() << std::endl;
			exit(0);
		}

		if (result["no-shader-hotreload"].as<bool>())
		{
			ShaderHotReload = false;
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
			TuiSsaa = std::clamp(TuiSsaa, 1u, 4u);
		}

		if (PresentMode > 3)
		{
			Throw(std::out_of_range("Invalid present mode."));
		}
	}
	catch ( const cxxopts::exceptions::exception& e)
	{
		std::cerr << e.what() << std::endl;
		exit(0);
	}
}

}
