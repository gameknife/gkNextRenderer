#include "Options.hpp"
#include "Utilities/Exception.hpp"
#include <cxxopts.hpp>
#include <iostream>

Options::Options(const int argc, const char* argv[])
{	
	cxxopts::Options options("options", "");
	options.add_options()
		("renderer",  "Renderer Type (0 = RayQuery, 1 = HybridDeferred, 2 = ModernDeferred, 3 = LegacyDeferred, 4 = VulkanBase).", cxxopts::value<uint32_t>(RendererType)->default_value("0"))
		("samples", "The number of ray samples per pixel.", cxxopts::value<uint32_t>(Samples)->default_value("8"))
		("bounces", "The general limit number of bounces per ray.", cxxopts::value<uint32_t>(Bounces)->default_value("4"))
		("max-bounces", "The maximum bounces per ray.", cxxopts::value<uint32_t>(MaxBounces)->default_value("10"))
		("temporal", "The number of temporal frames.", cxxopts::value<uint32_t>(Temporal)->default_value("8"))
		("nodenoiser", "Not Use Denoiser.", cxxopts::value<bool>(NoDenoiser)->default_value("false"))
		("adaptivesample", "use adaptive sample to improve render quality.", cxxopts::value<bool>(AdaptiveSample)->default_value("false"))

		("load-scene", "The scene to load. absolute path or relative path to project root.", cxxopts::value<std::string>(SceneName)->default_value(""))
		("hdri", "The HDRI file to load.", cxxopts::value<std::string>(HDRIfile)->default_value(""))

		("gpu", "Explicitly set the usage gpu idx.", cxxopts::value<uint32_t>(GpuIdx)->default_value("0"))

		("width", "The framebuffer width.", cxxopts::value<uint32_t>(Width)->default_value("1280"))
		("height", "The framebuffer height.", cxxopts::value<uint32_t>(Height)->default_value("720"))
		("present-mode", "The present mode (0 = Immediate, 1 = MailBox, 2 = FIFO, 3 = FIFORelaxed).", cxxopts::value<uint32_t>(PresentMode)->default_value("3"))
		("fullscreen", "Toggle fullscreen vs windowed (default: windowed).", cxxopts::value<bool>(Fullscreen)->default_value("false"))

		("savefile", "Save screenshot every benchmark finish.", cxxopts::value<bool>(SaveFile)->default_value("false"))
		("renderdoc", "Attach renderdoc if avaliable.", cxxopts::value<bool>(RenderDoc)->default_value("false"))
		("forcesdr", "Force use SDR Display even supported.", cxxopts::value<bool>(ForceSDR)->default_value("false"))
		("locale", "Locale: en, zhCN, RU.", cxxopts::value<std::string>(locale)->default_value("en"))

		("h,help", "Print usage");
	auto result = options.parse(argc, argv);

	if (result.count("help"))
	{
		std::cout << options.help() << std::endl;
		exit(0);
	}

	if (PresentMode > 3)
	{
		Throw(std::out_of_range("Invalid present mode."));
	}
}

