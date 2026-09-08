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
    bool CheckerboardRendering = false;

    // Frame pacing. Under a vsync present mode the interval a frame is on screen for is the display
    // period, not the wall-clock gap between two CPU frames; the two only agree while the
    // presentation path releases swapchain images one per vblank. See Runtime::FFramePacer.
    bool FramePacing = true;
    bool FramePacingLimitBurst = true;

    // Wireframe overlay (show.wireframe). The pass draws the soft-mesh expanded primitive stream,
    // so it shows exactly the geometry that survived culling, at the LOD it was drawn with.
    bool WireframeXRay = false;
    uint32_t WireframeColorMode = 0; // 0 = uniform, 1 = tint by LOD level

    // Camera
    int CameraIdx;
    float CameraFarPlane = Assets::defaultCameraFarPlane;

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

    // Projected pixel extent at or above which a proxy draws at LOD0; each further level halves it.
    // 0 pins everything to LOD0, which is the A/B baseline for measuring what LOD actually buys.
    float LodBaseThreshold = 64.0f;
    // Multiplier applied to LodBaseThreshold for the sun shadow cascades. >1 makes shadows drop to
    // coarser levels sooner than the main view, which is safe because a cascade only needs the
    // silhouette, not surface detail.
    //
    // Defaults to 1 (same policy as the main view) because that is what was measured: in the scenes
    // available here the cascades already resolve to the coarsest level at the main-view threshold,
    // so raising this changes nothing. It is a knob for content that is not represented yet -- high
    // poly assets shadowed at close range, where a cascade would otherwise hold LOD0.
    float LodShadowThresholdScale = 1.0f;
    float GTAOThickness = 0.5f;
    int GTAODebugMode = 0;
    bool LightObjectScreenSpaceShadow = false;
    float LightObjectShadowDistance = 6.0f;
    uint32_t LightObjectMaxShadowedLights = 2;
    uint32_t LightObjectShadowSteps = 12;

    // Cascaded world-space light grid. 0 cascades disables it: every light query falls back to the
    // global CDF, which restores the pre-grid behaviour exactly.
    int LightGridCascadeCount = 3;
    float LightGridBaseCellSize = 2.0f;
    float LightGridCullThreshold = 1.0e-4f;

    float AtmosphereSkyViewLutScale = 1.0f;
    int AtmosphereDebugMode = 0;

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
    uint32_t AmbientCubeBakeTargetFps = 60;
    bool AmbientCubeHitDrivenResidency = false;
    bool AmbientCubeBounceHitAffectsResidency = false;
    uint32_t AmbientCubeEvictFrames = 180;
    uint32_t AmbientCubeGraceFrames = 30;
    float AmbientCubeHitMarkTileRatio = 0.25f;
    int AmbientCubeResidencyDebug = 0;
    // Multiplier on the indirect term only (SHARC cache hit + ambient-cube path terminal).
    // 1 = physically accumulated GI; raise it to lift bounce light without touching direct/sky.
    float IndirectIntensity = 1.0f;
    // Per-order weight on bounces past the first, so once-bounced sunlight and multi-bounce
    // fill can be dialed independently. SHARC (PathTracing) only. 1 = physical.
    float MultiBounceIntensity = 1.0f;
    bool StreamHDRTextures = true;
    // Required by NextEngine::IsEffectiveSharcEnabled; Android still overrides this to false.
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
