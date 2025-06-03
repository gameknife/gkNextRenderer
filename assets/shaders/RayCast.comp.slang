import Common;

[[vk::binding(0, 0)]]
RaytracingAccelerationStructure Scene;

[[vk::binding(1, 0)]]
RWStructuredBuffer<RayCastContext> RayIO;

[[vk::binding(4, 0)]]
StructuredBuffer<float> Vertices;

[[vk::binding(5, 0)]]
StructuredBuffer<uint> Indices;

[[vk::binding(6, 0)]]
StructuredBuffer<Material> Materials;

[[vk::binding(7, 0)]]
StructuredBuffer<uint2> Offsets;

[[vk::binding(8, 0)]]
StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(0, 1)]]
Texture2D TextureSamplers[];

[[vk::binding(1, 1)]]
SamplerState TextureSampler;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RayCastContext context = RayIO[dispatchThreadId.x];

    // main函数体由您自行转换
    FHardwareRayTracer tracer = { Vertices, Indices, Materials, Offsets, NodeProxies, Scene };

    Vertex hitVertex;
    NodeProxy hitNode;
    if (tracer.TraceRay(context.Origin.xyz, context.Direction.xyz, 10000, hitVertex, hitNode))
    {
        context.HitPoint = float4(hitVertex.Position, 1.0);
        context.Normal = float4(hitVertex.Normal, 0.0);
        context.Hitted = 1;
        context.T = length(hitVertex.Position - context.Origin.xyz);
        context.InstanceId = hitNode.instanceId; // todo: 这里hack过
        context.MaterialId = hitVertex.MaterialIndex;
    }
    else
    {
        context.Hitted = 0;
    }

    RayIO[dispatchThreadId.x] = context;
}
