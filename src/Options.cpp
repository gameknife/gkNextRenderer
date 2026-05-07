#include "Options.hpp"
#include "Utilities/Exception.hpp"
#include <cxxopts.hpp>
#include <iostream>

Options::Options(const int argc, const char* argv[])
{	
	cxxopts::Options options("options", "");
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
		("fastexit", "Enable fast exit by skipping task wait.", cxxopts::value<bool>(FastExit)->default_value("true"))
		("agent-validation", "Enable agent validation actions (auto screenshot).", cxxopts::value<bool>(AgentValidation)->default_value("false"))
		("keep-cpu-mesh-data", "Keep CPU mesh data for editor mode.", cxxopts::value<bool>(KeepCPUMeshData)->default_value("false"))
		("update-baseline", "Update visual test baseline images from the current run.", cxxopts::value<bool>(UpdateVisualTestBaseline)->default_value("false")->implicit_value("true"))
		("flappy-replay", "Run Flappy deterministic replay and write trace output.", cxxopts::value<bool>(FlappyReplay)->default_value("false")->implicit_value("true"))
		("hot-reload", "Enable runtime hot reload features.", cxxopts::value<bool>(HotReload)->default_value("true")->implicit_value("true"))
		("no-hot-reload", "Disable runtime hot reload features.", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
		("shader-hotreload", "Enable Slang shader hot reload.", cxxopts::value<bool>(ShaderHotReload)->default_value("true")->implicit_value("true"))
		("no-shader-hotreload", "Disable Slang shader hot reload.", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
		("shader-hotreload-interval", "Slang shader hot reload poll interval in seconds.", cxxopts::value<float>(ShaderHotReloadInterval)->default_value("0.5"))

		("test-gltf", "Run glTF robustness test from Khronos Sample Assets.", cxxopts::value<bool>(TestGltfRobustness)->default_value("false"))
		("test-gltf-filter", "Filter for glTF robustness test (partial name match).", cxxopts::value<std::string>(TestGltfFilter)->default_value(""))

		("h,help", "Print usage");
	try
	{
		auto result = options.parse(argc, argv);

		if (result.count("help"))
		{
			std::cout << options.help() << std::endl;
			exit(0);
		}

		if (result["no-hot-reload"].as<bool>())
		{
			HotReload = false;
			ShaderHotReload = false;
		}

		if (result["no-shader-hotreload"].as<bool>())
		{
			ShaderHotReload = false;
		}

		if (ShaderHotReloadInterval < 0.1f)
		{
			ShaderHotReloadInterval = 0.1f;
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
