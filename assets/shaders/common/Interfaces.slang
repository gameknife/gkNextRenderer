implementing PathTracing;

#include "PreProcessor.slang"

// Shared tracing constants and the core PathTracing interfaces:
// IPrimaryRayCaster / IRayTracer / IDirectIlluminator / IRenderer. Concrete
// implementations live in RayTracers.slang, PrimaryRayCasters.slang and
// PathTracingRenderer.slang.

static const float PT_MAX_TRACE_DISTANCE = 1000.f;
static const float FAST_MAX_TRACE_DISTANCE = 20.f;
static const float GENERAL_MAX_TRACE_DISTANCE = 50.f;
static const float TRACE_CORRECTION_OFFSET = 0.0f;
static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_FLAG_SELECTED = 0x40000000u;
static const uint OBJECT_ID_FLAG_HOVERED = 0x20000000u;
static const uint OBJECT_ID_FLAG_LOCKED = 0x10000000u;
static const uint OBJECT_ID_FLAG_DANGER = 0x08000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;

// emit primary ray, return hit-info
public interface IPrimaryRayCaster
{
    bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode, inout float3 outRayDir, inout float outRayLength, inout float2 outPixelOffset);
}

// ray tracer for rendering
public interface IRayTracer
{
    bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist);
    bool TraceOcclusion(float3 rayOrigin, float3 rayDir);
    bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon);
    float3 GetAreaLightShadowOrigin(float3 position, float3 normal, float3 lightPosition,
                                    float minimumNormalOffset);
    bool TraceAreaLightSegment(float3 rayOrigin, float3 rayTarget, float epsilon);
}

// direct illum
public interface IDirectIlluminator
{
    void DirectIlluminate(inout uint4 RandomSeed, FBSDFContext context,
                          inout FDirectLighting directLighting);

    // Primary-surface variant. Identical to DirectIlluminate unless a screen-space resampler
    // (ReSTIR) replaces part of the estimate; pixel addresses the per-pixel reservoir.
    void DirectIlluminatePrimary(inout uint4 RandomSeed, int2 pixel, FBSDFContext context,
                                 inout FDirectLighting directLighting);
}

// renderer
public interface IRenderer
{
    void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inAlbedo, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor, inout float4 FinalReflection);
}
