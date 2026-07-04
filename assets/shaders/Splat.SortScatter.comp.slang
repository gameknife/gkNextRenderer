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

groupshared uint sharedBuckets[256];
groupshared uint sharedVisible[256];

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID,
          uint3 groupId : SV_GroupID)
{
    const uint index = dispatchThreadId.x;
    uint* splatBuckets = (uint*)push.bucketCursorAddress;
    uint bucket = 0xffffffffu;
    if (index < push.count)
    {
        bucket = splatBuckets[index];
    }
    const bool visible = bucket != 0xffffffffu;
    sharedBuckets[groupThreadId.x] = bucket;
    sharedVisible[groupThreadId.x] = visible ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();
    if (!visible) return;

    const uint group = min(groupId.x, push.groupCount - 1);
    uint localRank = 0;
    for (uint previous = 0; previous < groupThreadId.x; ++previous)
    {
        if (sharedVisible[previous] != 0u && sharedBuckets[previous] == bucket)
        {
            ++localRank;
        }
    }
    uint* bucketOffsets = (uint*)push.bucketOffsetAddress;
    uint* groupBucketPrefix = (uint*)push.groupBucketOffsetAddress;
    const uint previousGroupCount = group > 0
        ? groupBucketPrefix[(group - 1) * push.bucketCount + bucket]
        : 0u;
    const uint outputIndex = bucketOffsets[bucket] + previousGroupCount + localRank;
    uint* sortedIndices = (uint*)push.sortedIndexAddress;
    sortedIndices[outputIndex] = index;
}
