import Shader.Types;

struct GaussianSplat
{
    float4 positionOpacity;
    float4 covariance0;
    float4 covariance1;
    float4 sh0;
    uint4 metadata;
};

struct SplatModelState
{
    float4x4 world;
    float4 parameters;
    float4 lightingParameters;
};

struct PushConstants
{
    uint4 header;
    uint64_t cameraAddress;
    uint64_t splatAddress;
    uint64_t modelStateAddress;
    uint64_t sortedIndexAddress;
    uint64_t bucketCountAddress;
    uint64_t bucketOffsetAddress;
    // Reused as the per-splat bucket cache address; no cursor buffer is needed by the stable path.
    uint64_t bucketCursorAddress;
    uint64_t groupBucketCountAddress;
    uint64_t groupBucketOffsetAddress;
    uint64_t drawIndirectAddress;
    uint count;
    uint width;
    uint height;
    uint bucketCount;
    float nearPlane;
    float farPlane;
    uint groupCount;
    uint groupSize;
};

[[vk::push_constant]] PushConstants push;

bool GetVisibleBucket(uint index, out uint bucket)
{
    bucket = 0;
    GaussianSplat* splats = (GaussianSplat*)push.splatAddress;
    SplatModelState* modelStates = (SplatModelState*)push.modelStateAddress;
    UniformBufferObject* camera = (UniformBufferObject*)push.cameraAddress;
    const GaussianSplat splat = splats[index];
    const SplatModelState model = modelStates[splat.metadata.w];
    if (model.parameters.y < 0.5 || model.parameters.x <= 0.0) return false;
    const float3 worldPosition = mul(model.world, float4(splat.positionOpacity.xyz, 1.0)).xyz;
    const float4 viewPosition = mul(camera[0].ModelView, float4(worldPosition, 1.0));
    const float4 clip = mul(camera[0].ProjectionUnJit, viewPosition);
    if (viewPosition.z >= -1e-4 || clip.w <= 0.0) return false;
    const float2 ndc = clip.xy / clip.w;
    if (any(abs(ndc) > float2(1.2))) return false;
    const float depth = saturate((-viewPosition.z - push.nearPlane) / max(push.farPlane - push.nearPlane, 1e-4));
    bucket = min((uint)(depth * push.bucketCount), push.bucketCount - 1);
    return true;
}

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= push.count) return;
    uint bucket;
    if (!GetVisibleBucket(index, bucket)) return;
    uint* splatBuckets = (uint*)push.bucketCursorAddress;
    splatBuckets[index] = bucket;
    uint* bucketCounts = (uint*)push.bucketCountAddress;
    InterlockedAdd(bucketCounts[bucket], 1);
    const uint group = min(index / max(push.groupSize, 1), push.groupCount - 1);
    uint* groupBucketCounts = (uint*)push.groupBucketCountAddress;
    InterlockedAdd(groupBucketCounts[group * push.bucketCount + bucket], 1);
}
