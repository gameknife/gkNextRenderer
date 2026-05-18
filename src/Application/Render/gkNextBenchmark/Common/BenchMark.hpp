#pragma once

#include <fstream>
#include "Rendering/VulkanBaseRenderer.hpp"

class BenchMarker final
{
public:
	BenchMarker();
	~BenchMarker();
	
	void OnSceneStart( double nowInSeconds );
	bool OnTick( double nowInSeconds, Vulkan::VulkanBaseRenderer* renderer );
	void OnReport(Vulkan::VulkanBaseRenderer* renderer, const std::string& SceneName);
	void Report(Vulkan::VulkanBaseRenderer* renderer_, int fps, const std::string& sceneName, bool upload_screen, bool save_screen);
	// Benchmark stats
	int32_t benchUnit_{};
	double time_{};
	double sceneInitialTime_{};
	double periodInitialTime_{};
	uint32_t periodTotalFrames_{};
	uint32_t benchmarkTotalFrames_{};
	std::ofstream benchmarkCsvReportFile;
};
