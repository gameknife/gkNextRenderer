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

	// Camera
	float FieldOfView;
	float Aperture;
	float FocusDistance;

	float SkyRotation;

	// Profiler
	bool ShowHeatmap;
	float HeatmapScale;

	// UI
	bool ShowSettings;
	bool ShowOverlay;

	// Performance
	bool UseCheckerBoardRendering;
	int TemporalFrames;

	// Denoise
	int DenoiseIteration;
	float ColorPhi;
	float DepthPhi;
	float NormalPhi;

	float PaperWhiteNit;
	
	inline const static float FieldOfViewMinValue = 10.0f;
	inline const static float FieldOfViewMaxValue = 90.0f;

	bool RequiresAccumulationReset(const UserSettings& prev) const
	{
		return
			UseCheckerBoardRendering != prev.UseCheckerBoardRendering ||
			IsRayTraced != prev.IsRayTraced ||
			AccumulateRays != prev.AccumulateRays ||
			NumberOfBounces != prev.NumberOfBounces ||
			FieldOfView != prev.FieldOfView ||
			Aperture != prev.Aperture ||
			FocusDistance != prev.FocusDistance ||
				SkyRotation != prev.SkyRotation ||
			TemporalFrames != prev.TemporalFrames;
	}
};
