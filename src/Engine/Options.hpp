#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace Runtime::Config
{

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
	bool AgentValidationUI{};
	uint32_t AgentValidationFrames{90};
	std::string AgentValidationOutput{"screenshots/agent_validation"};
	bool HiddenWindow{};
	bool DisableStreamline{};
	bool RemoteMode{};
	bool RemoteShowWindow{};
	std::string RemoteBind{"0.0.0.0"};
	uint32_t RemoteHttpPort{8088};
	uint32_t RemotePort{8089};
	uint32_t RemoteBitrateKbps{4000};
	uint32_t RemoteFps{30};
	uint32_t RemoteWidth{};
	uint32_t RemoteHeight{};
	std::string RemoteEncoder{"auto"};
	bool KeepCPUMeshData{};  // 保留CPU网格数据（编辑器模式需要）
	bool HighPrecisionProgressiveHistory{}; // progressive accumulation/history 使用高精度缓冲
	bool UpdateVisualTestBaseline{};
	bool FlappyReplay{};
	bool ShaderHotReload{true};
	float ShaderHotReloadInterval{0.5f};
	std::vector<std::string> CVarOverrides{};
	std::string locale{};


	
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

}

extern Runtime::Config::Options* GOption;
