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
};

struct PushConstants
{
    uint64_t cameraAddress;
    uint64_t splatAddress;
    uint64_t modelStateAddress;
    uint64_t sortedIndexAddress;
    uint64_t bucketCountAddress;
    uint64_t bucketOffsetAddress;
    uint64_t bucketCursorAddress;
    uint64_t drawIndirectAddress;
    uint count;
    uint width;
    uint height;
    uint bucketCount;
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
    const float4 clip = mul(camera[0].Projection, viewPosition);
    if (viewPosition.z >= -1e-4 || clip.w <= 0.0) return false;
    const float2 ndc = clip.xy / clip.w;
    if (any(abs(ndc) > float2(1.2))) return false;
    const float depth = saturate(clip.z / clip.w);
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
    uint* bucketCursors = (uint*)push.bucketCursorAddress;
    uint outputIndex;
    InterlockedAdd(bucketCursors[bucket], 1, outputIndex);
    uint* sortedIndices = (uint*)push.sortedIndexAddress;
    sortedIndices[outputIndex] = index;
}
