#pragma once
#include "Assets/GPU/UniformBuffer.hpp"

#include "Assets/Core/Model.hpp"

struct UserSettings final
{
	int32_t RendererType;
	
	// Scene
	int SceneIndex {};
    float LDrawLduToWorldScale = 0.001f;

	// Renderer
	int32_t NumberOfSamples;
	uint32_t NumberOfBounces;
	uint32_t MaxNumberOfBounces;
	bool AdaptiveSample;
	float AdaptiveVariance;
	int AdaptiveSteps;
	bool TAA {};
	bool FastGather = false;
	uint32_t SuperResolution = 1;
	bool DLSS = false;
	bool DLSSRR = false;
	int BakeSpeedLevel = 1; // 0: realtime 1: normal 2: low

	// Camera
	int CameraIdx;

	// Profiler
	float HeatmapScale;

	// UI
	bool ShowSettings;
	bool ShowOverlay;
    bool BorderlessFullscreen = false;

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

    bool TickPhysics = true;
    bool TickAnimation = true;
    float SceneEpsilonScale = 1.0f;
    float AmbientCubeUnit = 0.25f;
    float AmbientCubeOffsetX = 0.0f;
    float AmbientCubeOffsetY = 0.0f;
    float AmbientCubeOffsetZ = 0.0f;
    int AmbientCubeCascadeCount = 3;
    float AmbientCubeCascadeRatio = 2.0f;
    bool UseAmbientCubePropagation = true;
    bool UseGpuAmbientCubeSdf = false;
};
