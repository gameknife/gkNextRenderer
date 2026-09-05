// Converts compact-pass counters to Vulkan indirect arguments without a device-address resource
// table. The host binds each output as a normal RWStructuredBuffer.
import Shader.Types;

[[vk::binding(7, 0)]] RWStructuredBuffer<uint> Counters;
[[vk::binding(8, 0)]] RWStructuredBuffer<VkDrawIndirectCommand> DrawArgs;
[[vk::binding(9, 0)]] RWStructuredBuffer<VkDispatchIndirectCommand> DispatchArgs;

struct CompatibilitySoftMeshCullPushConstants
{
    float4x4 ViewProjection;
    uint RenderProxyCount;
    uint TriangleCapacity;
};
[[vk::push_constant]] CompatibilitySoftMeshCullPushConstants pushConstants;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint triangleCount = min(Counters[0], pushConstants.TriangleCapacity);
    const uint visibleItemCount = min(Counters[1], uint(MAX_RENDER_PROXIES));

    DrawArgs[0].vertexCount = triangleCount * 3u;
    DrawArgs[0].instanceCount = 1u;
    DrawArgs[0].firstVertex = 0u;
    DrawArgs[0].firstInstance = 0u;

    // One 64-lane group expands one compacted proxy. Zero is a valid no-op dispatch count.
    DispatchArgs[0].x = visibleItemCount;
    DispatchArgs[0].y = 1u;
    DispatchArgs[0].z = 1u;
}
