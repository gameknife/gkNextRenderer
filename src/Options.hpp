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
	bool SaveFile{};
	bool RenderDoc{};
	bool NoDenoiser{};
	bool ForceSDR{};
	bool ReferenceMode{};
	uint32_t SuperResolution{};
	bool DLSS{};
	bool DLSSRR{};
	bool ForceNoRT{};
	bool ForceSoftGen{};
	bool HardwareQuery{};
	bool Validation{};
	bool FastExit{true};
	bool AgentValidation{};
	bool KeepCPUMeshData{};  // 保留CPU网格数据（编辑器模式需要）
	std::string locale{};

	// Renderer options.
	uint32_t Samples{};
	uint32_t Bounces{};
	uint32_t MaxBounces{};
	uint32_t RendererType{};
	uint32_t Temporal{};

	bool AdaptiveSample{};
	
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

	// Test options
	bool TestGltfRobustness{};
	std::string TestGltfFilter{};
};

extern Options* GOption;
