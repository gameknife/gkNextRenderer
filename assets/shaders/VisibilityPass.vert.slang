import Common;

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(1, 0)]]
StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(2, 0)]]
StructuredBuffer<uint> Reorders;

[[vk::binding(3, 0)]]
StructuredBuffer<float4> Vertices;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint2 primitive_index : PRIMITIVE_INDEX;
};

[shader("vertex")]
VertexOutput main(uint vertexIndex: SV_VertexID, uint baseVertex: SV_StartVertexLocation, uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absouluteInstanceIdx = instanceIndex + instanceBase;
    NodeProxy proxy = NodeProxies[absouluteInstanceIdx];
    float4 Position = Vertices[Reorders[vertexIndex]];
    output.position = mul(mul(Camera.ViewProjection, proxy.worldTS), float4(Position.xyz, 1.0));
    output.primitive_index = uint2(absouluteInstanceIdx + 1, vertexIndex - baseVertex);
    return output;
}