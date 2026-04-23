#include "Runtime/Config/EngineCVars.hpp"
#include "Runtime/Config/EngineCVars.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Config/ShowFlags.hpp"
#include "Runtime/Config/UserSettings.hpp"
#include "Runtime/Engine.hpp"
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

        auto* renderer = engine->GetRendererPtr();
        if (renderer)
        {
            renderer->RequestRecreateSwapChain();
        }
    }

    void ApplyBorderlessFullscreenIfPossible(NextEngine* engine, const UserSettings& settings)
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
    void RegisterEngineCVars(FCVarSystem& cvars, UserSettings& settings, ShowFlags& showFlags, NextEngine* engine)
    {
        GK_CVAR_UINT("r.temporalFrames", settings, TemporalFrames, 16, ECVarFlags::Archive,
                     "Temporal accumulation frames");
        GK_CVAR_INT("r.samples", settings, NumberOfSamples, 8, ECVarFlags::Archive,
                    "Samples per pixel");
        GK_CVAR_UINT("r.bounces", settings, NumberOfBounces, 5, ECVarFlags::Archive,
                     "Ray bounce count");
        GK_CVAR_INT("r.rendererType", settings, RendererType, 0, ECVarFlags::Archive,
                    "Renderer type (0=PathTracing,1=SoftTracing,2=PureAmbient,3=VoxelTracing)");
        GK_CVAR_UINT("r.maxBounces", settings, MaxNumberOfBounces, 10, ECVarFlags::Archive,
                     "Maximum ray bounce count");
        GK_CVAR_BOOL("r.denoiser", settings, Denoiser, false, ECVarFlags::Archive,
                     "Enable denoiser");
        GK_CVAR_FLOAT("r.denoiseSigma", settings, DenoiseSigma, 0.5f, ECVarFlags::Archive,
                      "Denoise sigma");
        GK_CVAR_FLOAT("r.denoiseSigmaLum", settings, DenoiseSigmaLum, 10.0f, ECVarFlags::Archive,
                      "Denoise sigma (luminance)");
        GK_CVAR_FLOAT("r.denoiseSigmaNormal", settings, DenoiseSigmaNormal, 0.1f, ECVarFlags::Archive,
                      "Denoise sigma (normal)");
        GK_CVAR_INT("r.denoiseSize", settings, DenoiseSize, 5, ECVarFlags::Archive,
                    "Denoise kernel size");
        GK_CVAR_UINT_CB("r.superResolution", settings, SuperResolution, 0, ECVarFlags::Archive,
                        "Super resolution mode (0-4)", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL_CB("r.dlss", settings, DLSS, false, ECVarFlags::Archive,
                        "Enable NVIDIA DLSS", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL_CB("r.dlssrr", settings, DLSSRR, false, ECVarFlags::Archive,
                        "Enable NVIDIA DLSS Ray Reconstruction", std::bind(RequestSwapChainIfPossible, engine));
        GK_CVAR_BOOL("r.taa", settings, TAA, true, ECVarFlags::Archive,
                     "Enable temporal anti-aliasing");
        GK_CVAR_BOOL("r.adaptiveSample", settings, AdaptiveSample, false, ECVarFlags::Archive,
                     "Enable adaptive sampling");
        GK_CVAR_FLOAT("r.adaptiveVariance", settings, AdaptiveVariance, 6.0f, ECVarFlags::Archive,
                      "Adaptive sampling variance");
        GK_CVAR_INT("r.adaptiveSteps", settings, AdaptiveSteps, 4, ECVarFlags::Archive,
                    "Adaptive sampling steps");
        GK_CVAR_BOOL("r.fastGather", settings, FastGather, false, ECVarFlags::Archive,
                     "Enable fast gather");
        GK_CVAR_INT("r.bakeSpeedLevel", settings, BakeSpeedLevel, 1, ECVarFlags::Archive,
                    "Bake speed level (0=realtime,1=normal,2=low)");
        GK_CVAR_FLOAT("r.heatmapScale", settings, HeatmapScale, 1.0f, ECVarFlags::Archive,
                      "Profiler heatmap scale");
        GK_CVAR_BOOL("r.checkerboard", settings, UseCheckerBoardRendering, false, ECVarFlags::Archive,
                     "Enable checkerboard rendering");
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
        GK_CVAR_BOOL("sys.ambientCubeGpuSdf", settings, UseGpuAmbientCubeSdf, false, ECVarFlags::Archive,
                     "Use GPU jump-flood distance field rebuild for ambient cube voxels");

        GK_CVAR_BOOL("show.debugLighting", showFlags, DebugDraw_Lighting, false, ECVarFlags::None,
                     "Debug draw lighting");
        GK_CVAR_BOOL("show.debugBoundingBox", showFlags, DebugDraw_BoundingBox, false, ECVarFlags::None,
                     "Debug draw bounding box");
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
