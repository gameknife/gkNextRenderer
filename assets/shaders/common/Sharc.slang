implementing PathTracing;

#include "PreProcessor.slang"

// Availability is judged per-feature on the extras table fields, not on the table pointer:
// the table also exists when only ReSTIR is enabled (see FTracingExtras). The null check
// is an explicit branch on purpose — scalar && does not guarantee short-circuiting here, and
// the field load must never execute against a null table pointer.
public bool SharcIsAvailable()
{
    FTracingExtras* extras = Bindless.GetGpuscene().TracingExtras;
    if (extras == (FTracingExtras*)0)
    {
        return false;
    }
    return extras[0].HashEntries != 0;
}

public SharcRuntimeParameters SharcGetRuntimeParameters()
{
    FTracingExtras* resources = Bindless.GetGpuscene().TracingExtras;
    return ((SharcRuntimeParameters*)resources[0].Parameters)[0];
}

#ifdef GK_ENABLE_OFFICIAL_SHARC

#ifndef SHARC_UPDATE
#define SHARC_UPDATE 1
#endif
#ifndef SHARC_QUERY
#define SHARC_QUERY 1
#endif
#define SHARC_ENABLE_SH_ENCODING 0
#define SHARC_ENABLE_RESPONSIVE_LIGHTING 0
#define SHARC_ENABLE_CACHE_RESAMPLING 0
#define SHARC_USE_FP16 0
#define SHARC_ENABLE_GLSL 0
#define HASH_GRID_COMPACT 0
#define HASH_GRID_ENABLE_64_BIT_ATOMICS 0
#define RW_STRUCTURED_BUFFER(name, type) type* name
#define BUFFER_AT_OFFSET(name, offset) name[offset]
#include "../third_party/sharc/SharcCommon.h"

SharcParameters SharcBuildParameters()
{
    FTracingExtras* resources = Bindless.GetGpuscene().TracingExtras;
    SharcRuntimeParameters runtime = SharcGetRuntimeParameters();

    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    float4 eye = mul(camera.ModelViewInverse, float4(0, 0, 0, 1));

    SharcParameters parameters;
    parameters.hashGridParameters.cameraPosition = eye.xyz;
    parameters.hashGridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
    parameters.hashGridParameters.sceneScale = max(runtime.SceneScale, 0.001f);
    parameters.hashGridParameters.levelBias = runtime.LevelBias;
    parameters.hashGridData.capacity = runtime.EntryCount;
    parameters.hashGridData.hashEntriesBuffer = (HashGridKey*)resources[0].HashEntries;
    parameters.hashGridData.lockBuffer = (uint*)resources[0].LockBuffer;
    parameters.radianceScale = max(runtime.RadianceScale, 1.0f);
    parameters.accumulationBuffer = (SharcAccumulationData*)resources[0].Accumulation;
    parameters.resolvedBuffer = (SharcPackedData*)resources[0].Resolved;
    return parameters;
}

SharcHitData SharcMakeHitData(float3 worldPosition, float3 normal)
{
    SharcHitData hitData;
    hitData.positionWorld = worldPosition;
    hitData.normalWorld = normalize(normal);
    return hitData;
}

public float SharcGetVoxelSizeAt(float3 worldPosition)
{
    SharcParameters parameters = SharcBuildParameters();
    uint level = HashGridGetLevel(worldPosition, parameters.hashGridParameters);
    return HashGridGetVoxelSize(level, parameters.hashGridParameters);
}

public float3 SharcDebugOccupancy(uint2 pixelPosition, uint2 screenSize)
{
    return HashGridDebugOccupancy(pixelPosition, screenSize, SharcBuildParameters().hashGridData);
}

public float3 SharcDebugSampleStale(uint2 pixelPosition, uint2 screenSize)
{
    if (!SharcIsAvailable())
    {
        return float3(0, 0, 0);
    }

    SharcParameters parameters = SharcBuildParameters();
    const uint elementSize = 7u;
    const uint borderSize = 1u;
    const uint blockSize = elementSize + borderSize;
    uint rowNum = screenSize.y / blockSize;
    uint rowIndex = pixelPosition.y / blockSize;
    uint columnIndex = pixelPosition.x / blockSize;
    uint elementIndex = (columnIndex / HASH_GRID_HASH_MAP_BUCKET_SIZE) * (rowNum * HASH_GRID_HASH_MAP_BUCKET_SIZE) +
        rowIndex * HASH_GRID_HASH_MAP_BUCKET_SIZE + (columnIndex % HASH_GRID_HASH_MAP_BUCKET_SIZE);

    if (elementIndex >= parameters.hashGridData.capacity ||
        (pixelPosition.x % blockSize) >= elementSize ||
        (pixelPosition.y % blockSize) >= elementSize)
    {
        return float3(0, 0, 0);
    }

    HashGridKey hashGridKey = BUFFER_AT_OFFSET(parameters.hashGridData.hashEntriesBuffer, elementIndex);
    if (hashGridKey == HASH_GRID_INVALID_HASH_KEY)
    {
        return float3(0, 0, 0);
    }

    SharcRuntimeParameters runtime = SharcGetRuntimeParameters();
    SharcVoxelData voxelData = SharcUnpackVoxelData(BUFFER_AT_OFFSET(parameters.resolvedBuffer, elementIndex));
    float stale = saturate(float(voxelData.staleFrameNum) / max(float(runtime.StaleFrameMax), 1.0f));
    float samples = saturate(log2(max(voxelData.accumulatedSampleNum, 0.0f) + 1.0f) / 3.0f);
    float frames = saturate(float(voxelData.accumulatedFrameNum) / min(max(float(runtime.AccumulatedFrameMax), 1.0f), 8.0f));
    return float3(0.2f, 0.2f, 0.2f) + float3(stale, samples, frames) * 4.0f;
}

public void SharcAccumulate(float3 worldPosition, float3 normal, float3 radiance)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    SharcState state;
    SharcInit(state);
    SharcUpdateHit(SharcBuildParameters(), state, SharcMakeHitData(worldPosition, normal), max(radiance, float3(0, 0, 0)), 0.5f);
}

void SharcInitPathState(inout SharcState state)
{
    SharcInit(state);
}

bool SharcUpdatePathHit(inout SharcState state, float3 worldPosition, float3 normal, float3 directLighting, float random)
{
    if (!SharcIsAvailable())
    {
        return false;
    }

    return SharcUpdateHit(
        SharcBuildParameters(),
        state,
        SharcMakeHitData(worldPosition, normal),
        max(directLighting, float3(0, 0, 0)),
        random);
}

void SharcUpdatePathMiss(inout SharcState state, float3 radiance)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    SharcUpdateMiss(SharcBuildParameters(), state, max(radiance, float3(0, 0, 0)));
}

void SharcSetPathThroughput(inout SharcState state, float3 throughput)
{
    SharcSetThroughput(state, max(throughput, float3(0, 0, 0)));
}

public bool SharcQuery(float3 worldPosition, float3 normal, out float4 radiance)
{
    radiance = float4(0, 0, 0, 0);
    if (!SharcIsAvailable())
    {
        return false;
    }

    float3 cachedRadiance = float3(0, 0, 0);
    if (!SharcGetCachedRadiance(SharcBuildParameters(), SharcMakeHitData(worldPosition, normal), cachedRadiance, true))
    {
        return false;
    }

    radiance = float4(cachedRadiance, 1);
    return true;
}

public void SharcResolveOfficialEntry(uint entryIndex)
{
    if (!SharcIsAvailable())
    {
        return;
    }

    SharcRuntimeParameters runtime = SharcGetRuntimeParameters();
    SharcResolveParameters resolveParameters;
    resolveParameters.cameraPositionPrev = runtime.CameraPositionPrev.xyz;
    resolveParameters.accumulationFrameNum = runtime.AccumulatedFrameMax;
    resolveParameters.responsiveFrameNum = runtime.ResponsiveFrameMax;
    resolveParameters.staleFrameNumMax = runtime.StaleFrameMax;
    resolveParameters.frameIndex = runtime.FrameIndex;

    SharcResolveEntry(entryIndex, SharcBuildParameters(), resolveParameters);
}

// Segment throughput ratio (after / before), only needed by the SHARC update cache.
float3 SharcComputeSegmentThroughput(float3 rayColorBefore, float3 rayColorAfter)
{
    return float3(
        rayColorBefore.x > 1e-6f ? rayColorAfter.x / rayColorBefore.x : 0.0f,
        rayColorBefore.y > 1e-6f ? rayColorAfter.y / rayColorBefore.y : 0.0f,
        rayColorBefore.z > 1e-6f ? rayColorAfter.z / rayColorBefore.z : 0.0f);
}

float SharcMaterialRoughness(Vertex vertex)
{
    Material mat = Bindless.GetGpuscene().GetMaterial(vertex.MaterialIndex);
    float roughness = mat.Fuzziness;
    if (mat.MRATextureId >= 0)
    {
        float4 mra = Bindless.GetSampleTexture(mat.MRATextureId).SampleLevel(vertex.TexCoord, 0);
        roughness = roughness * mra.g;
    }
    return roughness;
}

// IRadianceCache implementation feeding the SHARC update pass.
public struct FSharcUpdateCache : IRadianceCache
{
    public float UpdateSampleRatio = 1.0f;
    SharcState state;

    [mutating] public void BeginPath()
    {
        SharcInitPathState(state);
    }

    public bool ShouldShadePixel(int2 pixel, UniformBufferObject camera)
    {
        if (UpdateSampleRatio <= 0.0f)
        {
            return false;
        }
        if (UpdateSampleRatio >= 1.0f)
        {
            return true;
        }

        const uint tileSize = 5u;
        const uint tileSampleCount = tileSize * tileSize;
        uint activeSlots = clamp(uint(ceil(UpdateSampleRatio * float(tileSampleCount))), 1u, tileSampleCount);
        uint tileIndex = uint(pixel.x % int(tileSize)) + uint(pixel.y % int(tileSize)) * tileSize;
        uint frameOffset = (camera.TotalFrames * 7u) % tileSampleCount;
        uint rotatedTileIndex = (tileIndex + frameOffset) % tileSampleCount;
        return rotatedTileIndex < activeSlots;
    }

    public bool WantsDirectLighting() { return true; }

    [mutating] public bool OnSurfaceHit(float3 position, float3 normal, float3 directLighting, inout uint4 randomSeed)
    {
        return SharcUpdatePathHit(state, position, normal, directLighting, RandomFloat(randomSeed));
    }

    [mutating] public void OnSegmentThroughput(float3 rayColorBefore, float3 rayColorAfter)
    {
        SharcSetPathThroughput(state, SharcComputeSegmentThroughput(rayColorBefore, rayColorAfter));
    }

    [mutating] public void OnMiss(float3 radiance)
    {
        SharcUpdatePathMiss(state, radiance);
    }

    [mutating] public bool TryQueryRadiance(Vertex vertex, bool hitReflect, bool hitMetal,
                                            uint bounce, float segmentLength, out float4 radiance)
    {
        radiance = float4(0, 0, 0, 0);
        return false;
    }
}

// IRadianceCache implementation that short-circuits paths with SHARC-cached radiance.
public struct FSharcQueryCache : IRadianceCache
{
    public float RoughnessMin = 0.35f;
    public uint MinBounce = 1u;
    public uint DebugMode = 0u;

    [mutating] public void BeginPath() {}

    public bool ShouldShadePixel(int2 pixel, UniformBufferObject camera) { return true; }

    public bool WantsDirectLighting() { return false; }

    [mutating] public bool OnSurfaceHit(float3 position, float3 normal, float3 directLighting, inout uint4 randomSeed)
    {
        return true;
    }

    [mutating] public void OnSegmentThroughput(float3 rayColorBefore, float3 rayColorAfter) {}

    [mutating] public void OnMiss(float3 radiance) {}

    bool CanQueryAtBounce(Vertex vertex, bool hitReflect, bool hitMetal, uint bounce, float segmentLength)
    {
        if (bounce < MinBounce || hitReflect || hitMetal)
        {
            return false;
        }

        Material mat = Bindless.GetGpuscene().GetMaterial(vertex.MaterialIndex);
        if (mat.MaterialModel == MaterialDielectric || mat.MaterialModel == MaterialDiffuseLight)
        {
            return false;
        }

        if (SharcMaterialRoughness(vertex) < RoughnessMin)
        {
            return false;
        }

        float voxelSize = SharcGetVoxelSizeAt(vertex.Position);
        return voxelSize <= 0.0f || segmentLength >= voxelSize;
    }

    [mutating] public bool TryQueryRadiance(Vertex vertex, bool hitReflect, bool hitMetal,
                                            uint bounce, float segmentLength, out float4 radiance)
    {
        radiance = float4(0, 0, 0, 0);
        if (!CanQueryAtBounce(vertex, hitReflect, hitMetal, bounce, segmentLength))
        {
            return false;
        }

        float4 cachedRadiance = float4(0, 0, 0, 0);
        if (SharcQuery(vertex.Position, vertex.Normal, cachedRadiance))
        {
            radiance = DebugMode == 1u ? float4(0, 1, 0, 1) : cachedRadiance;
            return true;
        }
        else if (DebugMode == 2u)
        {
            radiance = float4(1, 0, 0, 1);
            return true;
        }
        return false;
    }
}

#else

public struct SharcState
{
    public uint placeholder;
};

public float SharcGetVoxelSizeAt(float3 worldPosition)
{
    return 0.0f;
}

public float3 SharcDebugOccupancy(uint2 pixelPosition, uint2 screenSize)
{
    return float3(0, 0, 0);
}

public float3 SharcDebugSampleStale(uint2 pixelPosition, uint2 screenSize)
{
    return float3(0, 0, 0);
}

public void SharcAccumulate(float3 worldPosition, float3 normal, float3 radiance)
{
}

void SharcInitPathState(inout SharcState state)
{
    state.placeholder = 0;
}

bool SharcUpdatePathHit(inout SharcState state, float3 worldPosition, float3 normal, float3 directLighting, float random)
{
    return false;
}

void SharcUpdatePathMiss(inout SharcState state, float3 radiance)
{
}

void SharcSetPathThroughput(inout SharcState state, float3 throughput)
{
}

public bool SharcQuery(float3 worldPosition, float3 normal, out float4 radiance)
{
    radiance = float4(0, 0, 0, 0);
    return false;
}

public void SharcResolveOfficialEntry(uint entryIndex)
{
}

#endif
