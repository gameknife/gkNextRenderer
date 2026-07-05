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
    uint groupSize;
};

[[vk::push_constant]] PushConstants push;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint* bucketCounts = (uint*)push.bucketCountAddress;
    uint* bucketOffsets = (uint*)push.bucketOffsetAddress;
    uint offset = 0;
    for (uint reverseBucket = 0; reverseBucket < push.bucketCount; ++reverseBucket)
    {
        const uint bucket = push.bucketCount - 1 - reverseBucket;
        bucketOffsets[bucket] = offset;
        offset += bucketCounts[bucket];
    }

    uint* draw = (uint*)push.drawIndirectAddress;
    draw[0] = 6;
    draw[1] = offset;
    draw[2] = 0;
    draw[3] = 0;
}
