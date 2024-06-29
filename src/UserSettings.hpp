#pragma once

struct UserSettings final
{
	// Application
	bool Benchmark;

	// Benchmark
	bool BenchmarkNextScenes{};
	uint32_t BenchmarkMaxTime{};
	
	// Scene
	int SceneIndex;

	// Renderer
	bool IsRayTraced;
	bool AccumulateRays;
	uint32_t NumberOfSamples;
	uint32_t NumberOfBounces;
	uint32_t MaxNumberOfSamples;
	bool AdaptiveSample;
	float AdaptiveVariance;
	int MaxAdaptiveSample;

	// Camera
	float FieldOfView;
	float Aperture;
	float FocusDistance;

	float SkyRotation;
	float SunRotation;
	float SunLuminance;
	float SkyIntensity;
	int SkyIdx;

	// Profiler
	bool ShowVisualDebug;
	float HeatmapScale;

	// UI
	bool ShowSettings;
	bool ShowOverlay;

	// Performance
	bool UseCheckerBoardRendering;
	int TemporalFrames;

	// Denoise
	bool Denoiser;

	float PaperWhiteNit;
	
	inline const static float FieldOfViewMinValue = 10.0f;
	inline const static float FieldOfViewMaxValue = 90.0f;

	uint32_t RR_MIN_DEPTH;
};
