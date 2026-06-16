#pragma once
#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include "Engine/Assets/Core/Model.hpp"

namespace Runtime::Config
{

struct UserSettings final
{
	int32_t RendererType;
	
	// Scene
	int SceneIndex {};
    float LDrawLduToWorldScale = 0.001f;
    float ScadToWorldScale = 1.0f;

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
    float AmbientCubePoolBrickRatio = 0.66f;
    bool UseGpuAmbientCubeSdf = false;
    bool StreamHDRTextures = true;
    bool SharcEnable = false;
    uint32_t SharcEntriesPow2 = 21;
    float SharcUpdateSampleRatio = 0.25f;
    int SharcDebugMode = 0;
    uint32_t SharcQueryMinBounce = 1;
    float SharcQueryRoughnessMin = 0.35f;
    float SharcSceneScale = 100.0f;
    float SharcLevelBias = 0.0f;
    float SharcRadianceScale = 1000.0f;
    uint32_t SharcAccumulatedFrameMax = 64;
    uint32_t SharcResponsiveFrameMax = 8;
    uint32_t SharcStaleFrameMax = 180;
};

}
