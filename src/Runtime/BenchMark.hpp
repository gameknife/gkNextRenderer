#pragma once

#include <fstream>
#include "Vulkan/VulkanBaseRenderer.hpp"

class BenchMarker final
{
public:
	BenchMarker();
	~BenchMarker();
	
	void OnSceneStart( double nowInSeconds );
	bool OnTick( double nowInSeconds, Vulkan::VulkanBaseRenderer* renderer );
	void OnReport(Vulkan::VulkanBaseRenderer* renderer, const std::string& SceneName);
	static void SaveSwapChainToFileFast(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int x, int y, int width, int height);
	static void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int x, int y, int width, int height);
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
