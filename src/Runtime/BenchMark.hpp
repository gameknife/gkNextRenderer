#pragma once

#include "Vulkan/VulkanBaseRenderer.hpp"

class BenchMarker
{
public:
	void OnSceneStart( double nowInSeconds );
	bool OnTick( double nowInSeconds, Vulkan::VulkanBaseRenderer* renderer );
	void OnReport();
	void Report(Vulkan::VulkanBaseRenderer* renderer_, int fps, const std::string& sceneName, bool upload_screen, bool save_screen);
	// Benchmark stats
	double time_{};
	double sceneInitialTime_{};
	double periodInitialTime_{};
	uint32_t periodTotalFrames_{};
	uint32_t benchmarkTotalFrames_{};
	uint32_t benchmarkNumber_{0};
	//std::ofstream benchmarkCsvReportFile;
};
