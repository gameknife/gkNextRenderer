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
	bool TAA {};
	bool FastGather = false;
	uint32_t PresentMode = 3;
	uint32_t SuperResolution = 1;
	bool DLSS = false;
    bool FSR = false;
	bool DLSSRR = false;
	bool DLSSG = false;
	uint32_t DLSSGFrameMultiplier = 2;
	uint32_t DLSSGFrameLimitFps = 0;
    uint32_t DLSSJitterFrames = 16;
    bool DLSSJitterInvertY = false;
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
	uint32_t TemporalFrames;
    uint32_t SplatBucketCount = 4096;
    uint32_t SplatMaxCount = 0;
    float SplatSigma = 3.0f;

	// Denoise (variance-guided a-trous wavelet; see PipelineCommon::AtrousDenoiser)
	bool Denoiser;
	int DenoiseAtrousIterations;    // diffuse wavelet iterations: quality/perf knob (higher = smoother, slower)
	int DenoiseAtrousSpecularIterations; // specular wavelet iterations (usually lower than diffuse)
	float DenoiseAtrousSigmaLuma;   // variance-guided luminance edge-stop (lower = sharper, noisier)
	float DenoiseAtrousNormalPower; // a-trous normal edge-stop exponent
	float DenoiseSigmaDepth;        // planar depth tolerance (multiples of local depth slope)
	float DenoiseSpecFootprint;     // specular filter radius (pixels) per unit roughness

	// ReProject history clamp (Phase A black-dot fix; see Process.ReProject.comp.slang).
	float ReprojectClampGammaHi = 2.5f;    // tight (low-confidence) upper YCoCg luma box half-width in sigmas
	float ReprojectClampGammaLo = 5.0f;    // tight lower box half-width (kept looser than upper to avoid black dots)
	float ReprojectClampFloor = 0.5f;      // relative luma floor (history luma >= floor * filtered mean)

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
    bool SharcEnable = true;
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
