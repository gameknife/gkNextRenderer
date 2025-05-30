#pragma once
#include "Assets/UniformBuffer.hpp"

#include "Assets/Model.hpp"

struct UserSettings final
{
	int RendererType;
	
	// Scene
	int SceneIndex {};

	// Renderer
	int32_t NumberOfSamples;
	int32_t NumberOfBounces;
	int32_t MaxNumberOfBounces;
	bool AdaptiveSample;
	float AdaptiveVariance;
	int AdaptiveSteps;
	bool TAA {};
	bool FastGather = false;
	bool FastInterpole = false;
	bool DebugDraw_Lighting = false;
	int BakeSpeedLevel = 1; // 0: realtime 1: normal 2: low

	// Camera
	bool RequestRayCast;
	int CameraIdx;

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

	bool ShowEdge;

	// HitResult
	Assets::RayCastResult HitResult;
};
