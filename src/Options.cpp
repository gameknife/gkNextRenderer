#include "Options.hpp"
#include "Utilities/Exception.hpp"
#include <cxxopts.hpp>
#include <iostream>

Options::Options(const int argc, const char* argv[])
{	
	cxxopts::Options options("options", "");
	options.add_options()
		("renderer",  "Renderer Type (0 = PathTracing, 1 = HybridTracing, 2 = SoftTracing, 3 = PureAmbient, 4 = VoxelTracing).", cxxopts::value<uint32_t>(RendererType)->default_value("0"))
		("samples", "The number of ray samples per pixel.", cxxopts::value<uint32_t>(Samples)->default_value("4"))
		("bounces", "The general limit number of bounces per ray.", cxxopts::value<uint32_t>(Bounces)->default_value("4"))
		("max-bounces", "The maximum bounces per ray.", cxxopts::value<uint32_t>(MaxBounces)->default_value("10"))
		("temporal", "The number of temporal frames.", cxxopts::value<uint32_t>(Temporal)->default_value("16"))
		("nodenoiser", "Not Use Denoiser.", cxxopts::value<bool>(NoDenoiser)->default_value("true"))
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
		("reference", "Reference Renderer Compare Mode.", cxxopts::value<bool>(ReferenceMode)->default_value("false"))
		("forcenort", "Forcing hardware raytracing not supported.", cxxopts::value<bool>(ForceNoRT)->default_value("false"))
		("superres", "SuperResolution: 50% / 66% / 100% -> 0 / 1 / 2.", cxxopts::value<uint32_t>(SuperResolution)->default_value("1"))


		("h,help", "Print usage");
	try
	{
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
	catch ( const cxxopts::exceptions::exception& e)
	{
		std::cerr << e.what() << std::endl;
		exit(0);
	}
}

