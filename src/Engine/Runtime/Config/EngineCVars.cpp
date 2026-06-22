#include "Engine/Runtime/Config/EngineCVars.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Engine.hpp"
#include <functional>

namespace
{
#define GK_CVAR_INT(name, obj, field, defaultValue, flags, desc) \
    cvars.RegisterInt(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_INT_CB(name, obj, field, defaultValue, flags, desc, cb) \
    cvars.RegisterInt(name, defaultValue, &obj.field, flags, desc, cb)
#define GK_CVAR_UINT(name, obj, field, defaultValue, flags, desc) \
    cvars.RegisterUInt(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_UINT_CB(name, obj, field, defaultValue, flags, desc, cb) \
    cvars.RegisterUInt(name, defaultValue, &obj.field, flags, desc, cb)
#define GK_CVAR_FLOAT(name, obj, field, defaultValue, flags, desc) \
    cvars.RegisterFloat(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_FLOAT_CB(name, obj, field, defaultValue, flags, desc, cb) \
    cvars.RegisterFloat(name, defaultValue, &obj.field, flags, desc, cb)
#define GK_CVAR_BOOL(name, obj, field, defaultValue, flags, desc) \
    cvars.RegisterBool(name, defaultValue, &obj.field, flags, desc)
#define GK_CVAR_BOOL_CB(name, obj, field, defaultValue, flags, desc, cb) \
    cvars.RegisterBool(name, defaultValue, &obj.field, flags, desc, cb)

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
        GK_CVAR_UINT("r.temporalFrames", settings, TemporalFrames, 16, ECVarFlags::Archive,
                     "Temporal accumulation frames");
        GK_CVAR_INT("r.samples", settings, NumberOfSamples, 4, ECVarFlags::Archive,
                    "Samples per pixel");
        GK_CVAR_UINT("r.bounces", settings, NumberOfBounces, 8, ECVarFlags::Archive,
                     "Ray bounce count");
        GK_CVAR_INT("r.rendererType", settings, RendererType, 0, ECVarFlags::Archive,
                    "Renderer type (0=PathTracing,1=SoftwareTracing,2=SoftwareModern,3=VoxelTracing,4=SoftwareModernNoAmbient)");
        GK_CVAR_UINT("r.maxBounces", settings, MaxNumberOfBounces, 10, ECVarFlags::Archive,
                     "Maximum ray bounce count");
        GK_CVAR_BOOL("r.denoiser", settings, Denoiser, false, ECVarFlags::Archive,
                     "Enable the variance-guided a-trous denoiser");
        GK_CVAR_INT("r.denoiseAtrousIterations", settings, DenoiseAtrousIterations, 3, ECVarFlags::Archive,
                    "Diffuse a-trous wavelet iterations: quality/perf knob (higher = smoother, slower; 1-6)");
        GK_CVAR_INT("r.denoiseAtrousSpecularIterations", settings, DenoiseAtrousSpecularIterations, 3, ECVarFlags::Archive,
                    "Specular a-trous wavelet iterations (lower is faster and preserves glossy detail; 0-6)");
        GK_CVAR_FLOAT("r.denoiseAtrousSigmaLuma", settings, DenoiseAtrousSigmaLuma, 4.0f, ECVarFlags::Archive,
                      "A-trous luminance edge-stop sigma (lower = sharper detail, more residual noise)");
        GK_CVAR_FLOAT("r.denoiseAtrousNormalPower", settings, DenoiseAtrousNormalPower, 64.0f, ECVarFlags::Archive,
                      "A-trous normal edge-stop exponent");
        GK_CVAR_FLOAT("r.denoiseSigmaDepth", settings, DenoiseSigmaDepth, 2.0f, ECVarFlags::Archive,
                      "A-trous planar depth tolerance (multiples of local depth slope)");
        GK_CVAR_FLOAT("r.denoiseSpecFootprint", settings, DenoiseSpecFootprint, 32.0f, ECVarFlags::Archive,
                      "Specular a-trous filter radius in pixels per unit roughness");
        GK_CVAR_BOOL("r.gtao.enable", settings, GTAOEnable, true, ECVarFlags::Archive,
                     "Enable half-resolution GTAO for SoftwareModernNoAmbient sky lighting");
        GK_CVAR_INT("r.gtao.quality", settings, GTAOQuality, 1, ECVarFlags::Archive,
                    "GTAO sampling quality (0=low 16 taps,1=medium 36 taps,2=high 64 taps,3=ultra 120 taps)");
        GK_CVAR_FLOAT("r.gtao.radius", settings, GTAORadius, 1.0f, ECVarFlags::Archive,
                      "GTAO world-space sampling radius");
        GK_CVAR_FLOAT("r.gtao.strength", settings, GTAOStrength, 1.5f, ECVarFlags::Archive,
                      "Master sky-occlusion strength: scales the combined GTAO + voxel skyVis darkening (1=natural, lower=lighter)");
        GK_CVAR_FLOAT("r.gtao.thickness", settings, GTAOThickness, 0.5f, ECVarFlags::Archive,
                      "GTAO depth-discontinuity thickness heuristic in world units");
        GK_CVAR_INT("r.gtao.debugMode", settings, GTAODebugMode, 0, ECVarFlags::Archive,
                    "GTAO debug mode (0=off,1=occlusion,2=unoccluded sky lighting,3=voxel skyVis,4=ao*skyVis)");
        GK_CVAR_BOOL("r.skyvis.enable", settings, SkyVisEnable, true, ECVarFlags::Archive,
                     "Enable voxel sky-visibility (large-scale/off-screen sky occlusion) for SoftwareModernNoAmbient");
        GK_CVAR_INT("r.skyvis.rayCount", settings, SkyVisRayCount, 16, ECVarFlags::Archive,
                    "Voxel sky-visibility hemisphere ray count per voxel (bake quality, e.g. 16/32)");
        GK_CVAR_FLOAT("r.skyvis.maxDistance", settings, SkyVisMaxDistance, 32.0f, ECVarFlags::Archive,
                      "Voxel sky-visibility soft-trace max distance in meters (large-scale range)");
        GK_CVAR_FLOAT("r.skyvis.strength", settings, SkyVisStrength, 1.0f, ECVarFlags::Archive,
                      "Voxel sky-visibility occlusion strength (0=off, 1=full baked occlusion)");
        GK_CVAR_INT("r.skyvis.combineMode", settings, SkyVisCombineMode, 0, ECVarFlags::Archive,
                    "Voxel sky-visibility combine with GTAO (0=mul,1=min,2=near-bright)");
        GK_CVAR_INT("r.skyvis.blur", settings, SkyVisBlurRadius, 2, ECVarFlags::Archive,
                    "Voxel sky-visibility bilateral blur radius in pixels in compose (0=off, smooths voxel-scale edges/noise)");
        GK_CVAR_FLOAT("r.reproject.clampGammaHi", settings, ReprojectClampGammaHi, 2.5f, ECVarFlags::Archive,
                      "ReProject history clamp: tight upper YCoCg-luma box half-width in sigmas (lower = less ghosting)");
        GK_CVAR_FLOAT("r.reproject.clampGammaLo", settings, ReprojectClampGammaLo, 5.0f, ECVarFlags::Archive,
                      "ReProject history clamp: tight lower box half-width in sigmas (kept looser than upper to avoid black dots)");
        GK_CVAR_FLOAT("r.reproject.clampFloor", settings, ReprojectClampFloor, 0.5f, ECVarFlags::Archive,
                      "ReProject history clamp: relative luma floor as a fraction of the filtered mean (guards against black dots)");
        GK_CVAR_UINT_CB("r.superResolution", settings, SuperResolution, 0, ECVarFlags::Archive,
                        "Super resolution mode (0-4)", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL_CB("r.dlss", settings, DLSS, false, ECVarFlags::Archive,
                        "Enable NVIDIA DLSS", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL_CB("r.fsr", settings, FSR, false, ECVarFlags::Archive,
                        "Enable FSR1 spatial upscaling", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL_CB("r.dlssrr", settings, DLSSRR, false, ECVarFlags::Archive,
                        "Enable NVIDIA DLSS Ray Reconstruction", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL_CB("r.dlssg", settings, DLSSG, false, ECVarFlags::Archive,
                        "Enable NVIDIA DLSS Frame Generation", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_UINT_CB("r.dlssg.multiplier", settings, DLSSGFrameMultiplier, 2, ECVarFlags::Archive,
                        "DLSS Frame Generation multiplier (2-4)", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_UINT("r.dlssg.frameLimitFps", settings, DLSSGFrameLimitFps, 0, ECVarFlags::Archive,
                     "Reflex base frame-rate limit while DLSS Frame Generation is enabled (0=unlimited)");
        GK_CVAR_UINT("r.dlss.jitterFrames", settings, DLSSJitterFrames, 16, ECVarFlags::Archive,
                     "DLSS projection jitter sequence length (clamped to 1-256)");
        GK_CVAR_BOOL("r.dlss.jitterInvertY", settings, DLSSJitterInvertY, false, ECVarFlags::Archive,
                     "Invert DLSS/TAA projection jitter Y for Streamline sign validation");
        GK_CVAR_BOOL("r.taa", settings, TAA, true, ECVarFlags::Archive,
                     "Enable temporal anti-aliasing");
        GK_CVAR_BOOL("r.fastGather", settings, FastGather, false, ECVarFlags::Archive,
                     "Enable fast gather");
        GK_CVAR_INT("r.bakeSpeedLevel", settings, BakeSpeedLevel, 1, ECVarFlags::Archive,
                    "Bake speed level (0=realtime,1=normal,2=low)");
        GK_CVAR_FLOAT("r.heatmapScale", settings, HeatmapScale, 1.0f, ECVarFlags::Archive,
                      "Profiler heatmap scale");
        GK_CVAR_UINT("r.splat.bucketCount", settings, SplatBucketCount, 4096, ECVarFlags::Archive,
                     "Gaussian splat depth-sort bucket count (clamped to 16-4096)");
        GK_CVAR_UINT("r.splat.maxCount", settings, SplatMaxCount, 0, ECVarFlags::Archive,
                     "Maximum Gaussian splats processed per frame (0=all)");
        GK_CVAR_FLOAT("r.splat.sigma", settings, SplatSigma, 3.0f, ECVarFlags::Archive,
                      "Gaussian billboard radius in standard deviations (clamped to 1-4)");
        GK_CVAR_FLOAT("r.paperWhiteNit", settings, PaperWhiteNit, 600.0f, ECVarFlags::Archive,
                      "Paper white nit");
        GK_CVAR_BOOL("ui.showSettings", settings, ShowSettings, true, ECVarFlags::Archive,
                     "Show settings panel");
        GK_CVAR_BOOL("ui.showOverlay", settings, ShowOverlay, true, ECVarFlags::Archive,
                     "Show overlay");
        GK_CVAR_BOOL("sys.tickPhysics", settings, TickPhysics, true, ECVarFlags::Archive,
                     "Tick physics system");
        GK_CVAR_BOOL("sys.tickAnimation", settings, TickAnimation, true, ECVarFlags::Archive,
                     "Tick animation system");
        GK_CVAR_BOOL_CB("sys.fullscreen", settings, BorderlessFullscreen, settings.BorderlessFullscreen,
                        ECVarFlags::Archive, "Toggle borderless fullscreen mode",
                        std::bind(ApplyBorderlessFullscreenIfPossible, engine, std::cref(settings)));
        GK_CVAR_FLOAT("sys.ldrawLduToWorldScale", settings, LDrawLduToWorldScale, 0.02f, ECVarFlags::Archive,
                      "World-space units represented by one LDraw LDU when loading .ldr/.mpd scenes");
        GK_CVAR_FLOAT("sys.scadToWorldScale", settings, ScadToWorldScale, 1.0f, ECVarFlags::Archive,
                      "Uniform world scale applied when loading .scad scenes (1 unit -> N meters)");
        GK_CVAR_FLOAT("sys.sceneEpsilonScale", settings, SceneEpsilonScale, 1.0f, ECVarFlags::Archive,
                      "Scene epsilon scale");
        GK_CVAR_FLOAT("sys.ambientCubeUnit", settings, AmbientCubeUnit, 0.25f, ECVarFlags::Archive,
                      "Ambient cube probe unit size in world units");
        GK_CVAR_FLOAT("sys.ambientCubeOffsetX", settings, AmbientCubeOffsetX, 0.0f, ECVarFlags::Archive,
                      "Ambient cube offset X in world units");
        GK_CVAR_FLOAT("sys.ambientCubeOffsetY", settings, AmbientCubeOffsetY, 0.0f, ECVarFlags::Archive,
                      "Ambient cube offset Y in world units");
        GK_CVAR_FLOAT("sys.ambientCubeOffsetZ", settings, AmbientCubeOffsetZ, 0.0f, ECVarFlags::Archive,
                      "Ambient cube offset Z in world units");
        GK_CVAR_INT("sys.ambientCubeCascadeCount", settings, AmbientCubeCascadeCount, 3, ECVarFlags::Archive,
                    "Ambient cube cascade count");
        GK_CVAR_FLOAT("sys.ambientCubeCascadeRatio", settings, AmbientCubeCascadeRatio, 2.0f, ECVarFlags::Archive,
                      "Ambient cube cascade ratio between levels");
        GK_CVAR_FLOAT("sys.ambientCubePoolBrickRatio", settings, AmbientCubePoolBrickRatio, 0.66f,
                      ECVarFlags::Archive,
                      "Ambient cube sparse pool capacity as a ratio of full bricks per cascade");
        GK_CVAR_BOOL("sys.ambientCubeGpuSdf", settings, UseGpuAmbientCubeSdf, false, ECVarFlags::Archive,
                     "Use GPU jump-flood distance field rebuild for ambient cube voxels");
        GK_CVAR_BOOL("sys.hdrTextureStreaming", settings, StreamHDRTextures, true, ECVarFlags::Archive,
                     "Keep inactive HDR environment textures at their lowest mip and promote the active sky on demand");
        GK_CVAR_BOOL("r.sharc.enable", settings, SharcEnable, true, ECVarFlags::Archive,
                     "Enable experimental SHARC path tracing radiance cache");
        GK_CVAR_UINT("r.sharc.entriesPow2", settings, SharcEntriesPow2, 21, ECVarFlags::Archive,
                     "SHARC cache entry count as log2");
        GK_CVAR_FLOAT("r.sharc.updateSampleRatio", settings, SharcUpdateSampleRatio, 0.25f, ECVarFlags::Archive,
                      "Fraction of pixels used by SHARC update pass");
        GK_CVAR_INT("r.sharc.debugMode", settings, SharcDebugMode, 0, ECVarFlags::Archive,
                    "SHARC debug mode (0=off,1=cache hit,2=cache miss,3=occupancy,4=radiance mosaic,5=stale/sample/frame heatmap)");
        GK_CVAR_UINT("r.sharc.queryMinBounce", settings, SharcQueryMinBounce, 1, ECVarFlags::Archive,
                     "Minimum bounce index for SHARC query");
        GK_CVAR_FLOAT("r.sharc.queryRoughnessMin", settings, SharcQueryRoughnessMin, 0.35f, ECVarFlags::Archive,
                      "Minimum material roughness for SHARC query");
        GK_CVAR_FLOAT("r.sharc.sceneScale", settings, SharcSceneScale, 100.0f, ECVarFlags::Archive,
                      "Official SHARC hash grid world-space scene scale; higher values produce smaller voxels");
        GK_CVAR_FLOAT("r.sharc.levelBias", settings, SharcLevelBias, 0.0f, ECVarFlags::Archive,
                      "Official SHARC hash grid LOD bias");
        GK_CVAR_FLOAT("r.sharc.radianceScale", settings, SharcRadianceScale, 1000.0f, ECVarFlags::Archive,
                      "Official SHARC integer accumulation radiance scale");
        GK_CVAR_UINT("r.sharc.accumulatedFrameMax", settings, SharcAccumulatedFrameMax, 64, ECVarFlags::Archive,
                     "Official SHARC maximum temporal accumulation frames");
        GK_CVAR_UINT("r.sharc.responsiveFrameMax", settings, SharcResponsiveFrameMax, 8, ECVarFlags::Archive,
                     "Official SHARC responsive temporal accumulation frames");
        GK_CVAR_UINT("r.sharc.staleFrameMax", settings, SharcStaleFrameMax, 180, ECVarFlags::Archive,
                     "Official SHARC stale frame eviction threshold");

        if (engine != nullptr)
        {
            Runtime::Config::Options& options = engine->GetOptions();
            GK_CVAR_BOOL("r.shader.hot_reload", options, ShaderHotReload, options.ShaderHotReload,
                         ECVarFlags::Archive, "Enable Slang shader hot reload");
            GK_CVAR_FLOAT("r.shader.hot_reload_interval", options, ShaderHotReloadInterval,
                          options.ShaderHotReloadInterval, ECVarFlags::Archive,
                          "Slang shader hot reload poll interval in seconds");
        }

        GK_CVAR_BOOL("show.debugLighting", showFlags, DebugDraw_Lighting, false, ECVarFlags::None,
                     "Debug draw lighting");
        GK_CVAR_BOOL("show.shadowCascadeCoverage", showFlags, DebugDraw_ShadowCascadeCoverage, false, ECVarFlags::None,
                     "Debug draw sun shadow cascade coverage");
        GK_CVAR_BOOL("show.debugBoundingBox", showFlags, DebugDraw_BoundingBox, false, ECVarFlags::None,
                     "Debug draw bounding box");
        GK_CVAR_BOOL("debug.physics.overlay", showFlags, DebugPhysicsOverlay, false, ECVarFlags::None,
                     "Show physics debug overlay");
        GK_CVAR_BOOL("debug.graphics.panel", showFlags, DebugGraphicsPanel, false, ECVarFlags::None,
                     "Show graphics debug panel");
        GK_CVAR_BOOL("debug.profile.overlay", showFlags, DebugProfileOverlay, false, ECVarFlags::None,
                     "Show CPU profile debug overlay");
        GK_CVAR_BOOL("show.debugPhysicsBodies", showFlags, DebugDraw_PhysicsBodies, false, ECVarFlags::None,
                     "Debug draw physics bodies");
        GK_CVAR_BOOL("show.visualDebug", showFlags, ShowVisualDebug, false, ECVarFlags::None,
                     "Show visual debug");
        GK_CVAR_BOOL("show.edge", showFlags, ShowEdge, false, ECVarFlags::None,
                     "Show selected edge highlight");
        GK_CVAR_BOOL("show.debugSkeleton", showFlags, ShowDebugSkeleton, false, ECVarFlags::None,
                     "Show debug skeleton");
        GK_CVAR_BOOL("show.grid", showFlags, ShowGrid, true, ECVarFlags::None,
                     "Show grid");
        GK_CVAR_BOOL("show.wireframe", showFlags, ShowWireframe, false, ECVarFlags::None,
                     "Show wireframe");
        GK_CVAR_BOOL("show.gaussianSplats", showFlags, ShowGaussianSplats, true, ECVarFlags::None,
                     "Show Gaussian splat models");
    }

}

namespace
{
#undef GK_CVAR_INT
#undef GK_CVAR_INT_CB
#undef GK_CVAR_UINT
#undef GK_CVAR_UINT_CB
#undef GK_CVAR_FLOAT
#undef GK_CVAR_FLOAT_CB
#undef GK_CVAR_BOOL
#undef GK_CVAR_BOOL_CB
}
