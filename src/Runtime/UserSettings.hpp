#pragma once
#include "Assets/UniformBuffer.hpp"

#include "Assets/Model.hpp"

struct UserSettings final
{
	int32_t RendererType;
	
	// Scene
	int SceneIndex {};

	// Renderer
	int32_t NumberOfSamples;
	uint32_t NumberOfBounces;
	uint32_t MaxNumberOfBounces;
	bool AdaptiveSample;
	float AdaptiveVariance;
	int AdaptiveSteps;
	bool TAA {};
	bool FastGather = false;
	bool FastInterpole = false;
	bool DebugDraw_Lighting = false;
	bool DisableSpatialReuse = false;
	uint32_t SuperResolution = 1;
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
	uint32_t TemporalFrames;

	// Denoise
	bool Denoiser;
	float DenoiseSigma;
	float DenoiseSigmaLum;
	float DenoiseSigmaNormal;
	int DenoiseSize;

	float PaperWhiteNit;

	bool ShowEdge;
	bool TickPhysics = true;
	bool TickAnimation = false;
    float SceneEpsilonScale = 1.0f;
};
