#include "Options.hpp"
#include "Utilities/Exception.hpp"
#include <cxxopts.hpp>
#include <iostream>

Options::Options(const int argc, const char* argv[])
{
	const int lineLength = 120;

	// options_description renderer("Renderer options", lineLength);
	// renderer.add_options()
	// 	("renderer", value<uint32_t>(&RendererType)->default_value(0), "Renderer Type (0 = RayQuery, 1 = HybridDeferred， 2 = ModernDeferred, 3 = LegacyDeferred, 4 = VulkanBase).")
	// 	("samples", value<uint32_t>(&Samples)->default_value(8), "The number of ray samples per pixel.")
	// 	("bounces", value<uint32_t>(&Bounces)->default_value(4), "The general limit number of bounces per ray.")
	// 	("max-bounces", value<uint32_t>(&MaxBounces)->default_value(10), "The maximum bounces per ray.")
	// 	("temporal", value<uint32_t>(&Temporal)->default_value(8), "The number of temporal frames.")
	// 	("nodenoiser", bool_switch(&NoDenoiser)->default_value(false), "Not Use Denoiser.")
	// 	("adaptivesample", bool_switch(&AdaptiveSample)->default_value(false), "use adaptive sample to improve render quality.")
	//
 //    ;
 //
	// options_description scene("Scene options", lineLength);
	// scene.add_options()
	// 	("load-scene", value<std::string>(&SceneName)->default_value(""), "The scene to load. absolute path or relative path to project root.")
	// 	("hdri", value<std::string>(&HDRIfile)->default_value(""), "The HDRI file to load.")
	// 	;
 //
	// options_description vulkan("Vulkan options", lineLength);
	// vulkan.add_options()
	// 	("gpu", value<uint32_t>(&GpuIdx)->default_value(0), "Explicitly set the usage gpu idx.")
	// 	;
 //
	// options_description window("Window options", lineLength);
	// window.add_options()
	// 	("width", value<uint32_t>(&Width)->default_value(1280), "The framebuffer width.")
	// 	("height", value<uint32_t>(&Height)->default_value(720), "The framebuffer height.")
	// 	("present-mode", value<uint32_t>(&PresentMode)->default_value(3), "The present mode (0 = Immediate, 1 = MailBox, 2 = FIFO, 3 = FIFORelaxed).")
	// 	("fullscreen", bool_switch(&Fullscreen)->default_value(false), "Toggle fullscreen vs windowed (default: windowed).")
	// 	;
 //
	// options_description desc("Application options", lineLength);
	// desc.add_options()
	// 	("help", "Display help message.")
	// 	("savefile", bool_switch(&SaveFile)->default_value(false), "Save screenshot every benchmark finish.")
	// 	("renderdoc", bool_switch(&RenderDoc)->default_value(false), "Attach renderdoc if avaliable.")
	// 	("forcesdr", bool_switch(&ForceSDR)->default_value(false), "Force use SDR Display even supported.")
	// 	("locale", value<std::string>(&locale)->default_value("en"), "Locale: en, zhCN, RU.")
	// 	;
	//
	// desc.add(renderer);
	// desc.add(scene);
	// desc.add(vulkan);
	// desc.add(window);
 //
	// const positional_options_description positional;
	// variables_map vm;
	// store(command_line_parser(argc, argv).options(desc).positional(positional).run(), vm);
	// notify(vm);

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
	
	// if (PresentMode > 3)
	// {
	// 	Throw(std::out_of_range("invalid present mode"));
	// }
}

