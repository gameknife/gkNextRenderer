import Common;
import Bindless;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    float3 radiance = float3(0, 0, 0);
    float4 cachedRadiance = float4(0, 0, 0, 0);
    SharcAccumulate(float3(0, 0, 0), float3(0, 1, 0), radiance);
    SharcQuery(float3(0, 0, 0), float3(0, 1, 0), cachedRadiance);
    SharcResolveOfficialEntry(DTid.x);

    // Compile-spike: ensure both radiance-cache implementations type-check.
    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    uint4 seed = uint4(DTid.x, DTid.y, 0, 0);
    Vertex vertex = {};

    FSharcUpdateCache updateCache;
    updateCache.UpdateSampleRatio = 0.25f;
    updateCache.BeginPath();
    if (updateCache.ShouldShadePixel(int2(DTid.xy), camera) && updateCache.WantsDirectLighting())
    {
        updateCache.OnSurfaceHit(float3(0, 0, 0), float3(0, 1, 0), radiance, seed);
        updateCache.OnSegmentThroughput(float3(1, 1, 1), float3(0.5, 0.5, 0.5));
        updateCache.OnMiss(radiance);
        updateCache.TryQueryRadiance(vertex, false, false, 1u, 1.0f, cachedRadiance);
    }

    FSharcQueryCache queryCache;
    queryCache.RoughnessMin = 0.35f;
    queryCache.MinBounce = 1u;
    queryCache.DebugMode = 0u;
    queryCache.BeginPath();
    queryCache.ShouldShadePixel(int2(DTid.xy), camera);
    queryCache.WantsDirectLighting();
    queryCache.OnSurfaceHit(float3(0, 0, 0), float3(0, 1, 0), radiance, seed);
    queryCache.TryQueryRadiance(vertex, false, false, 1u, 1.0f, cachedRadiance);
}
