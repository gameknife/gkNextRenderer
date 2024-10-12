#pragma once
#include "Assets/UniformBuffer.hpp"

#include "Assets/Model.hpp"

struct UserSettings final
{
	int RendererType;
	// Application
	bool Benchmark;

	// Benchmark
	bool BenchmarkNextScenes{};
	uint32_t BenchmarkMaxTime{};
	uint32_t BenchmarkMaxFrame{};
	
	// Scene
	int SceneIndex;

	// Renderer
	bool AccumulateRays;
	int32_t NumberOfSamples;
	int32_t NumberOfBounces;
	int32_t MaxNumberOfBounces;
	bool AdaptiveSample;
	float AdaptiveVariance;
	int AdaptiveSteps;
	bool TAA;

	// Camera
	float FieldOfView;
	float RawFieldOfView;
	float Aperture;
	float FocusDistance;
	bool RequestRayCast;

	float SkyRotation;
	float SunRotation;
	float SunLuminance;
	float SkyIntensity;
	int SkyIdx, CameraIdx;

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
	float DenoiseSigma;
	float DenoiseSigmaLum;
	float DenoiseSigmaNormal;
	int DenoiseSize;

	float PaperWhiteNit;
	
	inline const static float FieldOfViewMinValue = 5.0f;
	inline const static float FieldOfViewMaxValue = 90.0f;
	
	std::vector<Assets::Camera> cameras;
	// HitResult
	Assets::RayCastResult HitResult;
};
