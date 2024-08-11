#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

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
	bool Benchmark{};
	bool SaveFile{};
	bool RenderDoc{};
	bool Denoiser{};
	bool ForceSDR{};
	bool Editor{};
	std::string locale{};
	
	// Benchmark options.
	bool BenchmarkNextScenes{};
	uint32_t BenchmarkMaxTime{};
	uint32_t BenchmarkMaxFrame{};

	// Renderer options.
	uint32_t Samples{};
	uint32_t Bounces{};
	uint32_t MaxBounces{};
	uint32_t RendererType{};
	uint32_t Temporal{};
	uint32_t RR_MIN_DEPTH{};
	bool AdaptiveSample{};
	
	// Scene options.
	uint32_t SceneIndex{};
	std::string SceneName{};
	std::string HDRIfile{};

	// Vulkan options
	uint32_t GpuIdx{};

	// Window options
	uint32_t Width{};
	uint32_t Height{};
	uint32_t PresentMode{};
	bool Fullscreen{};
};

inline const Options* GOption = nullptr;