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
    float LDrawLduToWorldScale = 0.02f;
    float ScadToWorldScale = 1.0f;

    // Renderer
    int32_t NumberOfSamples = 2;
    uint32_t NumberOfBounces;
    uint32_t MaxNumberOfBounces;
    bool ProgressiveRender = false;
    bool ExitAfterFirst = false;
    uint32_t PresentMode = 3;
    uint32_t SuperResolution = 5;
    int32_t UpscalerType = 4;
    float NativeTAAUHistoryWeight = 0.97f;
    float NativeTAAUSharpness = 0.25f;
    bool TemporalUpscalerPostFilter = true;
    uint32_t TemporalUpscalerPostFilterPasses = 3;
    float TemporalUpscalerPostFilterStrength = 0.65f;
    float TemporalUpscalerPostFilterLumaSigma = 0.10f;
    float TemporalUpscalerFireflySigma = 2.5f;
    bool ComposeFireflyClamp = true;
    bool FrameGeneration = false;
    uint32_t FrameGenerationMultiplier = 2;
    uint32_t FrameGenerationFrameLimitFps = 0;
    uint32_t UpscalerJitterFrames = 16;
    bool UpscalerJitterInvertY = false;
    int BakeSpeedLevel = 1; // 0: realtime 1: normal 2: low

    // Camera
    int CameraIdx;

    // Profiler
    float HeatmapScale;

    // UI
    bool ShowOverlay;
    bool BorderlessFullscreen = false;

    // Performance
    uint32_t TemporalFrames;

    // SwModernNoAmbient screen-space sky occlusion.
    bool GTAOEnable = true;
    int GTAOQuality = 1;
    float GTAORadius = 1.0f;
    float GTAOStrength = 5.0f; // master sky-occlusion strength
    float GTAOThickness = 0.5f;
    int GTAODebugMode = 0;

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
    float AmbientCubePoolBrickRatio = 0.5f;
    bool AmbientCubeHitDrivenResidency = false;
    bool AmbientCubeBounceHitAffectsResidency = false;
    uint32_t AmbientCubeEvictFrames = 180;
    uint32_t AmbientCubeGraceFrames = 30;
    float AmbientCubeHitMarkTileRatio = 0.25f;
    int AmbientCubeResidencyDebug = 0;
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
    // PathTracing/SoftwareTracing ReSTIR DI (docs/designs/pathtracing-restir-design.md)
    bool RestirEnable = false;
    uint32_t RestirCandidates = 8;
    bool RestirTemporal = true;
    uint32_t RestirMClamp = 160;
    bool RestirSpatial = true;
    uint32_t RestirSpatialSamples = 5;
    float RestirSpatialRadius = 16.0f;
    int RestirDebugMode = 0;
};

}
