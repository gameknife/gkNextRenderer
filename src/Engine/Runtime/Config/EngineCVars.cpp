#include "Engine/Runtime/Config/EngineCVars.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Engine.hpp"
#include <functional>

namespace
{
#define GK_CVAR_INT(name, obj, field, defaultValue, flags, desc) cvars.RegisterInt(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_INT_CB(name, obj, field, defaultValue, flags, desc, cb) cvars.RegisterInt(name, defaultValue, &obj.field, flags, desc, cb)
#define GK_CVAR_INT_RANGE(name, obj, field, defaultValue, flags, desc, minValue, maxValue) cvars.RegisterInt(name, defaultValue, &obj.field, flags, desc, nullptr, minValue, maxValue)
#define GK_CVAR_UINT(name, obj, field, defaultValue, flags, desc) cvars.RegisterUInt(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_UINT_CB(name, obj, field, defaultValue, flags, desc, cb) cvars.RegisterUInt(name, defaultValue, &obj.field, flags, desc, cb)
#define GK_CVAR_UINT_RANGE(name, obj, field, defaultValue, flags, desc, minValue, maxValue) cvars.RegisterUInt(name, defaultValue, &obj.field, flags, desc, nullptr, minValue, maxValue)
#define GK_CVAR_FLOAT(name, obj, field, defaultValue, flags, desc) cvars.RegisterFloat(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_FLOAT_CB(name, obj, field, defaultValue, flags, desc, cb) cvars.RegisterFloat(name, defaultValue, &obj.field, flags, desc, cb)
#define GK_CVAR_FLOAT_RANGE(name, obj, field, defaultValue, flags, desc, minValue, maxValue) cvars.RegisterFloat(name, defaultValue, &obj.field, flags, desc, nullptr, minValue, maxValue)
#define GK_CVAR_BOOL(name, obj, field, defaultValue, flags, desc) cvars.RegisterBool(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_BOOL_CB(name, obj, field, defaultValue, flags, desc, cb) cvars.RegisterBool(name, defaultValue, &obj.field, flags, desc, cb)

    void RequestSwapChainIfPossible(NextEngine* engine)
    {
        if (!engine)
        {
            return;
        }

        if (engine->GetEngineStatus() != NextRenderer::EApplicationStatus::Starting)
        {
            engine->GetRenderer().RequestRecreateSwapChain();
        }
    }

    void ApplyBorderlessFullscreenIfPossible(NextEngine* engine, const Runtime::Config::UserSettings& settings)
    {
        if (!engine)
        {
            return;
        }

        engine->SetBorderlessFullscreen(settings.BorderlessFullscreen);
    }
}

namespace NextCVar
{
    void RegisterEngineCVars(FCVarSystem& cvars, Runtime::Config::UserSettings& settings, Runtime::Config::ShowFlags& showFlags, NextEngine* engine)
    {
        GK_CVAR_UINT("r.temporalFrames", settings, TemporalFrames, 16, ECVarFlags::Archive, "Moving-object history rejection frames");
        GK_CVAR_INT("r.samples", settings, NumberOfSamples, 2, ECVarFlags::Archive, "Samples per pixel");
        GK_CVAR_UINT("r.bounces", settings, NumberOfBounces, 8, ECVarFlags::Archive, "Ray bounce count");
        GK_CVAR_BOOL("r.progressiveRender", settings, ProgressiveRender, false, ECVarFlags::Archive, "Enable progressive rendering while the camera is idle");
        GK_CVAR_INT("r.rendererType", settings, RendererType, 0, ECVarFlags::Archive, "Renderer type (0=PathTracing,1=SoftwareTracing,2=SoftwareModern,3=VoxelTracing,4=SoftwareModernNoAmbient)");
        GK_CVAR_UINT("r.maxBounces", settings, MaxNumberOfBounces, 10, ECVarFlags::Archive, "Maximum ray bounce count");
        GK_CVAR_BOOL("r.gtao.enable", settings, GTAOEnable, true, ECVarFlags::Archive, "Enable half-resolution GTAO for SoftwareModernNoAmbient sky lighting");
        GK_CVAR_INT("r.gtao.quality", settings, GTAOQuality, 1, ECVarFlags::Archive, "GTAO sampling quality (0=low 16 taps,1=medium 36 taps,2=high 64 taps,3=ultra 120 taps)");
        GK_CVAR_FLOAT("r.gtao.radius", settings, GTAORadius, 1.0f, ECVarFlags::Archive, "GTAO world-space sampling radius");
        GK_CVAR_FLOAT("r.gtao.strength", settings, GTAOStrength, 5.0f, ECVarFlags::Archive, "Master sky-occlusion strength: scales GTAO sky darkening (1=natural, lower=lighter)");
        GK_CVAR_FLOAT("r.gtao.thickness", settings, GTAOThickness, 0.5f, ECVarFlags::Archive, "GTAO depth-discontinuity thickness heuristic in world units");
        GK_CVAR_INT("r.gtao.debugMode", settings, GTAODebugMode, 0, ECVarFlags::Archive, "GTAO debug mode (0=off,1=occlusion,2=unoccluded sky lighting)");
        GK_CVAR_UINT_CB("r.upscaler.qualityMode", settings, SuperResolution, 5, ECVarFlags::Archive, "Upscaler quality mode (0=Quality,1=Balanced,2=Performance,3=Ultra Performance,4=Native/DLAA,5=Auto)", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_INT_CB("r.upscaler.type", settings, UpscalerType, 4, ECVarFlags::Archive, "Upscaler type (0=None,1=DLSS,2=DLSS Ray Reconstruction,3=FidelityFX FSR,4=Native TAAU,5=SGSR2)", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_FLOAT_RANGE("r.taau.historyWeight", settings, NativeTAAUHistoryWeight, 0.97f, ECVarFlags::Archive, "Native TAAU stable-history blend weight", 0.5, 0.98);
        GK_CVAR_FLOAT_RANGE("r.taau.sharpness", settings, NativeTAAUSharpness, 0.25f, ECVarFlags::Archive, "Native TAAU display-resolution adaptive sharpening", 0.0, 1.0);
        GK_CVAR_BOOL("r.upscaler.postFilter", settings, TemporalUpscalerPostFilter, true, ECVarFlags::Archive, "Apply display-resolution bilateral noise and firefly suppression after supported temporal upscalers");
        GK_CVAR_UINT_RANGE("r.upscaler.postFilterPasses", settings, TemporalUpscalerPostFilterPasses, 3, ECVarFlags::Archive, "Temporal upscaler post-filter a-trous pass count", 1, 4);
        GK_CVAR_FLOAT_RANGE("r.upscaler.postFilterStrength", settings, TemporalUpscalerPostFilterStrength, 0.65f, ECVarFlags::Archive, "Temporal upscaler post-filter blend strength", 0.0, 1.0);
        GK_CVAR_FLOAT_RANGE("r.upscaler.postFilterLumaSigma", settings, TemporalUpscalerPostFilterLumaSigma, 0.10f, ECVarFlags::Archive, "Temporal upscaler post-filter compressed-domain color/luminance edge threshold", 0.01, 0.5);
        GK_CVAR_FLOAT_RANGE("r.upscaler.fireflySigma", settings, TemporalUpscalerFireflySigma, 2.5f, ECVarFlags::Archive, "Temporal upscaler post-filter isolated highlight rejection threshold in neighborhood standard deviations", 1.0, 8.0);
        GK_CVAR_BOOL_CB("r.frameGeneration", settings, FrameGeneration, false, ECVarFlags::Archive, "Enable frame generation for the selected upscaler type", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_UINT_CB("r.frameGeneration.multiplier", settings, FrameGenerationMultiplier, 2, ECVarFlags::Archive, "Frame generation multiplier (2-4 when supported)", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_UINT("r.frameGeneration.frameLimitFps", settings, FrameGenerationFrameLimitFps, 0, ECVarFlags::Archive, "Base frame-rate limit while frame generation is enabled (0=unlimited)");
        GK_CVAR_UINT("r.upscaler.jitterFrames", settings, UpscalerJitterFrames, 16, ECVarFlags::Archive, "Fallback temporal upscaler projection jitter sequence length (clamped to 1-256)");
        GK_CVAR_BOOL("r.upscaler.jitterInvertY", settings, UpscalerJitterInvertY, false, ECVarFlags::Archive, "Invert temporal upscaler projection jitter Y for diagnostics");
        GK_CVAR_BOOL("r.taa", settings, TAA, true, ECVarFlags::Archive, "Enable temporal anti-aliasing");
        GK_CVAR_BOOL("r.tracing.exitAfterFirst", settings, ExitAfterFirst, false, ECVarFlags::Archive, "Terminate tracing after the first non-dielectric surface hit");
        GK_CVAR_INT("r.bakeSpeedLevel", settings, BakeSpeedLevel, 1, ECVarFlags::Archive, "Bake speed level (0=realtime,1=normal,2=low)");
        GK_CVAR_FLOAT("r.heatmapScale", settings, HeatmapScale, 1.0f, ECVarFlags::Archive, "Profiler heatmap scale");
        GK_CVAR_FLOAT("r.paperWhiteNit", settings, PaperWhiteNit, 600.0f, ECVarFlags::Archive, "Paper white nit");
        GK_CVAR_BOOL("ui.showOverlay", settings, ShowOverlay, true, ECVarFlags::Archive, "Show overlay");
        GK_CVAR_BOOL("sys.tickPhysics", settings, TickPhysics, true, ECVarFlags::Archive, "Tick physics system");
        GK_CVAR_BOOL("sys.tickAnimation", settings, TickAnimation, true, ECVarFlags::Archive, "Tick animation system");
        GK_CVAR_BOOL_CB("sys.borderlessFullscreen", settings, BorderlessFullscreen, settings.BorderlessFullscreen, ECVarFlags::Archive, "Toggle borderless fullscreen mode", std::bind(ApplyBorderlessFullscreenIfPossible, engine, std::cref(settings)));
        GK_CVAR_FLOAT("sys.ldrawLduToWorldScale", settings, LDrawLduToWorldScale, 0.02f, ECVarFlags::Archive, "World-space units represented by one LDraw LDU when loading .ldr/.mpd scenes");
        GK_CVAR_FLOAT("sys.scadToWorldScale", settings, ScadToWorldScale, 1.0f, ECVarFlags::Archive, "Uniform world scale applied when loading .scad scenes (1 unit -> N meters)");
        GK_CVAR_FLOAT("sys.sceneEpsilonScale", settings, SceneEpsilonScale, 1.0f, ECVarFlags::Archive, "Scene epsilon scale");
        GK_CVAR_FLOAT("sys.ambientCubeUnit", settings, AmbientCubeUnit, 0.25f, ECVarFlags::Archive, "Ambient cube probe unit size in world units");
        GK_CVAR_FLOAT("sys.ambientCubeOffsetX", settings, AmbientCubeOffsetX, 0.0f, ECVarFlags::Archive, "Ambient cube offset X in world units");
        GK_CVAR_FLOAT("sys.ambientCubeOffsetY", settings, AmbientCubeOffsetY, 0.0f, ECVarFlags::Archive, "Ambient cube offset Y in world units");
        GK_CVAR_FLOAT("sys.ambientCubeOffsetZ", settings, AmbientCubeOffsetZ, 0.0f, ECVarFlags::Archive, "Ambient cube offset Z in world units");
        GK_CVAR_INT("sys.ambientCubeCascadeCount", settings, AmbientCubeCascadeCount, 3, ECVarFlags::Archive, "Ambient cube cascade count");
        GK_CVAR_FLOAT("sys.ambientCubeCascadeRatio", settings, AmbientCubeCascadeRatio, 2.0f, ECVarFlags::Archive, "Ambient cube cascade ratio between levels");
        GK_CVAR_FLOAT("sys.ambientCubePoolBrickRatio", settings, AmbientCubePoolBrickRatio, 0.5f, ECVarFlags::Archive, "Ambient cube sparse pool capacity as a ratio of full bricks per cascade");
        GK_CVAR_BOOL("r.ambientCube.hitDrivenResidency", settings, AmbientCubeHitDrivenResidency, false, ECVarFlags::Archive, "Enable hit-driven ambient cube brick residency");
        GK_CVAR_BOOL("r.ambientCube.bounceHitAffectsResidency", settings, AmbientCubeBounceHitAffectsResidency, false, ECVarFlags::Archive, "Allow ambient cube bake bounce hits to keep bricks resident");
        GK_CVAR_UINT_RANGE("r.ambientCube.evictFrames", settings, AmbientCubeEvictFrames, 180, ECVarFlags::Archive, "Frames without a residency-driving hit before eviction", 30, 3600);
        GK_CVAR_UINT_RANGE("r.ambientCube.graceFrames", settings, AmbientCubeGraceFrames, 30, ECVarFlags::Archive, "Initial candidate residency grace period", 1, 600);
        GK_CVAR_FLOAT_RANGE("r.ambientCube.hitMarkTileRatio", settings, AmbientCubeHitMarkTileRatio, 0.25f, ECVarFlags::Archive, "Sparse query hit marking ratio", 0.01, 1.0);
        GK_CVAR_INT_RANGE("r.ambientCube.residencyDebug", settings, AmbientCubeResidencyDebug, 0, ECVarFlags::None, "Ambient residency debug (0=off,1=hit age,2=resident state)", 0, 2);
        GK_CVAR_BOOL("sys.hdrTextureStreaming", settings, StreamHDRTextures, true, ECVarFlags::Archive, "Keep inactive HDR environment textures at their lowest mip and promote the active sky on demand");
        GK_CVAR_BOOL("r.sharc.enable", settings, SharcEnable, true, ECVarFlags::Archive, "Enable experimental SHARC path tracing radiance cache");
        GK_CVAR_UINT("r.sharc.entriesPow2", settings, SharcEntriesPow2, 21, ECVarFlags::Archive, "SHARC cache entry count as log2");
        GK_CVAR_FLOAT("r.sharc.updateSampleRatio", settings, SharcUpdateSampleRatio, 0.25f, ECVarFlags::Archive, "Fraction of pixels used by SHARC update pass");
        GK_CVAR_INT("r.sharc.debugMode", settings, SharcDebugMode, 0, ECVarFlags::Archive, "SHARC debug mode (0=off,1=cache hit,2=cache miss,3=occupancy,4=radiance mosaic,5=stale/sample/frame heatmap)");
        GK_CVAR_UINT("r.sharc.queryMinBounce", settings, SharcQueryMinBounce, 1, ECVarFlags::Archive, "Minimum bounce index for SHARC query");
        GK_CVAR_FLOAT("r.sharc.queryRoughnessMin", settings, SharcQueryRoughnessMin, 0.35f, ECVarFlags::Archive, "Minimum material roughness for SHARC query");
        GK_CVAR_FLOAT("r.sharc.sceneScale", settings, SharcSceneScale, 100.0f, ECVarFlags::Archive, "Official SHARC hash grid world-space scene scale; higher values produce smaller voxels");
        GK_CVAR_FLOAT("r.sharc.levelBias", settings, SharcLevelBias, 0.0f, ECVarFlags::Archive, "Official SHARC hash grid LOD bias");
        GK_CVAR_FLOAT("r.sharc.radianceScale", settings, SharcRadianceScale, 1000.0f, ECVarFlags::Archive, "Official SHARC integer accumulation radiance scale");
        GK_CVAR_UINT("r.sharc.accumulatedFrameMax", settings, SharcAccumulatedFrameMax, 64, ECVarFlags::Archive, "Official SHARC maximum temporal accumulation frames");
        GK_CVAR_UINT("r.sharc.responsiveFrameMax", settings, SharcResponsiveFrameMax, 8, ECVarFlags::Archive, "Official SHARC responsive temporal accumulation frames");
        GK_CVAR_UINT("r.sharc.staleFrameMax", settings, SharcStaleFrameMax, 180, ECVarFlags::Archive, "Official SHARC stale frame eviction threshold");
        GK_CVAR_BOOL("r.restir.enable", settings, RestirEnable, false, ECVarFlags::Archive, "Enable ReSTIR DI resampling for PathTracing/SoftwareTracing primary-surface area light direct lighting");
        GK_CVAR_UINT_RANGE("r.restir.candidates", settings, RestirCandidates, 8, ECVarFlags::Archive, "ReSTIR initial candidate count per pixel", 1, 64);
        GK_CVAR_BOOL("r.restir.temporal", settings, RestirTemporal, true, ECVarFlags::Archive, "Enable ReSTIR temporal reservoir reuse");
        GK_CVAR_UINT_RANGE("r.restir.mClamp", settings, RestirMClamp, 160, ECVarFlags::Archive, "ReSTIR temporal reservoir M clamp", 1, 4096);
        GK_CVAR_BOOL("r.restir.spatial", settings, RestirSpatial, true, ECVarFlags::Archive, "Enable ReSTIR spatial reservoir reuse");
        GK_CVAR_UINT_RANGE("r.restir.spatialSamples", settings, RestirSpatialSamples, 5, ECVarFlags::Archive, "ReSTIR spatial reuse neighbor count", 1, 16);
        GK_CVAR_FLOAT_RANGE("r.restir.spatialRadius", settings, RestirSpatialRadius, 16.0f, ECVarFlags::Archive, "ReSTIR spatial reuse radius in pixels", 1.0, 128.0);
        GK_CVAR_INT_RANGE("r.restir.debugMode", settings, RestirDebugMode, 0, ECVarFlags::None, "ReSTIR debug (0=off,1=M heatmap,2=W,3=winner light id,4=reuse proxy)", 0, 4);
        GK_CVAR_BOOL("show.debugLighting", showFlags, DebugDraw_Lighting, false, ECVarFlags::None, "Debug draw lighting");
        GK_CVAR_BOOL("show.debugAreaLights", showFlags, DebugDraw_AreaLights, false, ECVarFlags::None, "Debug draw scene area lights");
        GK_CVAR_BOOL("show.debugBoundingBox", showFlags, DebugDraw_BoundingBox, false, ECVarFlags::None, "Debug draw bounding box");
        GK_CVAR_BOOL("debug.physics.overlay", showFlags, DebugPhysicsOverlay, false, ECVarFlags::None, "Show physics debug overlay");
        GK_CVAR_BOOL("debug.graphics.panel", showFlags, DebugGraphicsPanel, false, ECVarFlags::None, "Show graphics debug panel");
        GK_CVAR_BOOL("debug.cvar.panel", showFlags, DebugCVarPanel, false, ECVarFlags::None, "Show the developer CVar editor");
        GK_CVAR_BOOL("debug.profile.overlay", showFlags, DebugProfileOverlay, false, ECVarFlags::None, "Show CPU profile debug overlay");
        GK_CVAR_BOOL("show.debugPhysicsBodies", showFlags, DebugDraw_PhysicsBodies, false, ECVarFlags::None, "Debug draw physics bodies");
        GK_CVAR_BOOL("show.visualDebug", showFlags, ShowVisualDebug, false, ECVarFlags::None, "Show visual debug");
        GK_CVAR_BOOL("show.edge", showFlags, ShowEdge, false, ECVarFlags::None, "Show selected edge highlight");
        GK_CVAR_BOOL("show.debugSkeleton", showFlags, ShowDebugSkeleton, false, ECVarFlags::None, "Show debug skeleton");
        GK_CVAR_BOOL("show.grid", showFlags, ShowGrid, true, ECVarFlags::None, "Show grid");
        GK_CVAR_BOOL("show.wireframe", showFlags, ShowWireframe, false, ECVarFlags::None, "Show wireframe");
    }

}

namespace
{
#undef GK_CVAR_INT
#undef GK_CVAR_INT_CB
#undef GK_CVAR_INT_RANGE
#undef GK_CVAR_UINT
#undef GK_CVAR_UINT_CB
#undef GK_CVAR_UINT_RANGE
#undef GK_CVAR_FLOAT
#undef GK_CVAR_FLOAT_CB
#undef GK_CVAR_FLOAT_RANGE
#undef GK_CVAR_BOOL
#undef GK_CVAR_BOOL_CB
}
