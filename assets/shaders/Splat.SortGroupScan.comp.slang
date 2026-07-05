struct PushConstants
{
    uint4 header;
    uint64_t cameraAddress;
    uint64_t splatAddress;
    uint64_t modelStateAddress;
    uint64_t sortedIndexAddress;
    uint64_t bucketCountAddress;
    uint64_t bucketOffsetAddress;
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
    uint scanStride;
};

[[vk::push_constant]] PushConstants push;

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint bucket = dispatchThreadId.x;
    if (bucket >= push.bucketCount) return;

    uint* groupBucketCounts = (uint*)push.groupBucketCountAddress;
    uint* groupBucketOffsets = (uint*)push.groupBucketOffsetAddress;
    uint running = 0;
    for (uint group = 0; group < push.groupCount; ++group)
    {
        const uint index = group * push.bucketCount + bucket;
        running += groupBucketCounts[index];
        groupBucketOffsets[index] = running;
    }
}
