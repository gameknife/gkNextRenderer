// Fixed-descriptor primitive expansion. One workgroup owns one compacted proxy and writes
// uint2(proxyIndex, indexBufferOffset), which the compatibility vertex shader resolves through
// ordinary storage-buffer indexing.
//
// Storing an absolute index-buffer offset rather than a triangle ordinal is what keeps LOD out of
// the vertex stage: the cull pass already resolved which level's index range this proxy draws, so
// per-vertex work never consults the LOD table -- or ModelData::indexOffset -- again.
import Shader.Types;

[[vk::binding(5, 0)]] RWStructuredBuffer<uint2> ExpandedPrimitives;
[[vk::binding(6, 0)]] StructuredBuffer<uint4> VisibleItems;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint4 item = VisibleItems[groupId.x];
    for (uint triangleIndex = groupThreadId.x;
         triangleIndex < item.z;
         triangleIndex += 64u)
    {
        ExpandedPrimitives[item.x + triangleIndex] = uint2(item.y, item.w + triangleIndex * 3u);
    }
}
