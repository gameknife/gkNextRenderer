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
	bool ForceSDR{};
	bool ReferenceMode{};
	bool ForceNoRT{};
	bool ForceSoftGen{};
	bool HardwareQuery{};
	bool Validation{};
	bool FastExit{true};
	bool AgentValidation{};
	bool KeepCPUMeshData{};  // 保留CPU网格数据（编辑器模式需要）
	bool UpdateVisualTestBaseline{};
	bool FlappyReplay{};
	bool HotReload{true};
	bool ShaderHotReload{true};
	float ShaderHotReloadInterval{0.5f};
	std::string locale{};


	
	// Scene options.
	std::string SceneName{};
	std::string HDRIfile{};
	std::string QuickJSEntry{"assets/scripts/test.js"};

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
