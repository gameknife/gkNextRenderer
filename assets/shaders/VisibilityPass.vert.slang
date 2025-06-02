import Common;

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(1, 0)]]
StructuredBuffer<NodeProxy> NodeProxies;

struct VertexInput
{
    [[vk::location(0)]] float3 InPosition : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint primitive_index : PRIMITIVE_INDEX;
    [[vk::location(1)]] nointerpolation uint instance_index : INSTANCE_INDEX;
};

[shader("vertex")]
VertexOutput main(VertexInput input, uint vertexIndex: SV_VertexID, uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absouluteInstanceIdx = instanceIndex + instanceBase;
    NodeProxy proxy = NodeProxies[absouluteInstanceIdx];
    output.position = mul(mul(Camera.ViewProjection, proxy.worldTS), float4(input.InPosition, 1.0));
    output.primitive_index = (vertexIndex / 3);
    output.instance_index = absouluteInstanceIdx;
    
    return output;
}