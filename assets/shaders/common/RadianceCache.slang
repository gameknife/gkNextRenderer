implementing PathTracing;

#include "PreProcessor.slang"

// Generic radiance-cache abstraction used by FPathTracingRenderer.
// The renderer drives any IRadianceCache implementation without knowing whether
// a cache is active. Concrete implementations:
//   - FNullRadianceCache  (here)        : no-op, used by plain PT / software paths
//   - FSharcUpdateCache   (Sharc.slang) : feeds the SHARC update pass
//   - FSharcQueryCache    (Sharc.slang) : short-circuits paths with cached radiance
// Slang monomorphizes the cache argument, so the null cache compiles away to nothing.

public interface IRadianceCache
{
    // Reset per-sample path state before tracing a new path.
    [mutating] void BeginPath();

    // Per-pixel gate: returns false to skip the whole pixel this frame
    // (carries the SHARC update 5x5 sparse-sampling pattern).
    bool ShouldShadePixel(int2 pixel, UniformBufferObject camera);

    // Whether the cache needs per-hit direct lighting (decides if the renderer
    // actually traces a shadow ray for cache accounting).
    bool WantsDirectLighting();

    // Record a surface hit (with its direct lighting). Returns false when the
    // cache requests early path termination. The implementation draws its own
    // randoms from randomSeed only when it needs them, so non-caching paths keep
    // an identical random stream.
    [mutating] bool OnSurfaceHit(float3 position, float3 normal, float3 directLighting, inout uint4 randomSeed);

    // Record the throughput of the segment just scattered (ratio after/before).
    [mutating] void OnSegmentThroughput(float3 rayColorBefore, float3 rayColorAfter);

    // Radiance recorded when a path escapes (sky) or terminates on an ambient cube.
    [mutating] void OnMiss(float3 radiance);

    // Try to short-circuit the path at the current bounce using cached radiance.
    // Returns true when the path terminates here; radiance is the value to multiply in.
    [mutating] bool TryQueryRadiance(Vertex vertex, bool hitReflect, bool hitMetal,
                                     uint bounce, float segmentLength, out float4 radiance);
}

// No-op cache: every plain path-tracing / software path uses this. All calls
// inline away after monomorphization, so behaviour is bit-identical to a renderer
// without any cache plumbing.
public struct FNullRadianceCache : IRadianceCache
{
    [mutating] public void BeginPath() {}

    public bool ShouldShadePixel(int2 pixel, UniformBufferObject camera) { return true; }

    public bool WantsDirectLighting() { return false; }

    [mutating] public bool OnSurfaceHit(float3 position, float3 normal, float3 directLighting, inout uint4 randomSeed)
    {
        return true;
    }

    [mutating] public void OnSegmentThroughput(float3 rayColorBefore, float3 rayColorAfter) {}

    [mutating] public void OnMiss(float3 radiance) {}

    [mutating] public bool TryQueryRadiance(Vertex vertex, bool hitReflect, bool hitMetal,
                                            uint bounce, float segmentLength, out float4 radiance)
    {
        radiance = float4(0, 0, 0, 0);
        return false;
    }
}
