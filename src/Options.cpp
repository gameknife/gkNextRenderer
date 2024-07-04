#include "Options.hpp"
#include "SceneList.hpp"
#include "Utilities/Exception.hpp"
#include <boost/program_options.hpp>
#include <iostream>

using namespace boost::program_options;

Options::Options(const int argc, const char* argv[])
{
	const int lineLength = 120;
	
	options_description benchmark("Benchmark options", lineLength);
	benchmark.add_options()
		("next-scenes", bool_switch(&BenchmarkNextScenes)->default_value(false), "Load the next scene once the sample or time limit is reached.")
		("max-time", value<uint32_t>(&BenchmarkMaxTime)->default_value(10), "The benchmark time limit per scene (in seconds).")
		;

	options_description renderer("Renderer options", lineLength);
	renderer.add_options()
		("renderer", value<uint32_t>(&RendererType)->default_value(0), "Renderer Type (0 = RayTraced, 1 = ModernDeferred, 2 = LegacyDeferred, 3 = RayQuery, 4 = HybridDeferred, 5 = VulkanBase).")
		("samples", value<uint32_t>(&Samples)->default_value(1), "The number of ray samples per pixel.")
		("bounces", value<uint32_t>(&Bounces)->default_value(4), "The general limit number of bounces per ray.")
		("max-bounces", value<uint32_t>(&MaxBounces)->default_value(10), "The maximum bounces per ray.")
		("temporal", value<uint32_t>(&Temporal)->default_value(64), "The number of temporal frames.")
		("denoiser", bool_switch(&Denoiser)->default_value(false), "Use Denoiser.")
		("rr", value<uint32_t>(&RR_MIN_DEPTH)->default_value(2), "Russian roulette start from bounce.")
		("adaptivesample", bool_switch(&AdaptiveSample)->default_value(false), "use adaptive sample to improve render quality.")
	
    ;

	options_description scene("Scene options", lineLength);
	scene.add_options()
		("scene", value<uint32_t>(&SceneIndex)->default_value(0), "The scene to start with.")
		("load-scene", value<std::string>(&SceneName)->default_value(""), "The scene to load.")
		;

	options_description vulkan("Vulkan options", lineLength);
	vulkan.add_options()
		("gpu", value<uint32_t>(&GpuIdx)->default_value(0), "Explicitly set the usage gpu idx.")
		;

	options_description window("Window options", lineLength);
	window.add_options()
		("width", value<uint32_t>(&Width)->default_value(1280), "The framebuffer width.")
		("height", value<uint32_t>(&Height)->default_value(720), "The framebuffer height.")
		("present-mode", value<uint32_t>(&PresentMode)->default_value(3), "The present mode (0 = Immediate, 1 = MailBox, 2 = FIFO, 3 = FIFORelaxed).")
		("fullscreen", bool_switch(&Fullscreen)->default_value(false), "Toggle fullscreen vs windowed (default: windowed).")
		;

	options_description desc("Application options", lineLength);
	desc.add_options()
		("help", "Display help message.")
		("benchmark", bool_switch(&Benchmark)->default_value(false), "Run the application in benchmark mode.")
		("savefile", bool_switch(&SaveFile)->default_value(false), "Save screenshot every benchmark finish.")
		("renderdoc", bool_switch(&RenderDoc)->default_value(false), "Attach renderdoc if avaliable.")
		("forcesdr", bool_switch(&ForceSDR)->default_value(false), "Force use SDR Display even supported.")
		;

	desc.add(benchmark);
	desc.add(renderer);
	desc.add(scene);
	desc.add(vulkan);
	desc.add(window);

	const positional_options_description positional;
	variables_map vm;
	store(command_line_parser(argc, argv).options(desc).positional(positional).run(), vm);
	notify(vm);

	if (vm.count("help"))
	{
		std::cout << desc << std::endl;
		Throw(Help());
	}

	// if (SceneIndex >= SceneList::AllScenes.size()) // w/o loaded scene
	// {
	// 	Throw(std::out_of_range("scene index is too large"));
	// }

	if (PresentMode > 3)
	{
		Throw(std::out_of_range("invalid present mode"));
	}
}

