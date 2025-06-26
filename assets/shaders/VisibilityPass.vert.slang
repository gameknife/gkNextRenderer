import Common;

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(1, 0)]]
StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(2, 0)]]
StructuredBuffer<uint> Reorders;

[[vk::binding(3, 0)]]
StructuredBuffer<GPUVertex> Vertices;

[[vk::binding(4, 0)]]
StructuredBuffer<ModelData> Models;

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
VertexOutput main(VertexInput input, uint vertexIndex: SV_VertexID, uint baseVertex: SV_StartVertexLocation, uint instanceIndex: SV_InstanceID, uint instanceBase: SV_StartInstanceLocation)
{
    VertexOutput output;
    uint absouluteInstanceIdx = instanceIndex + instanceBase;
    NodeProxy proxy = NodeProxies[absouluteInstanceIdx];
    ModelData model = Models[proxy.modelId];
    GPUVertex vertex = Vertices[Reorders[model.reorderOffset + vertexIndex - baseVertex]];
    output.position = mul(mul(Camera.ViewProjection, proxy.worldTS), float4(vertex.PositionX, vertex.PositionY, vertex.PositionZ, 1.0));
    output.primitive_index = model.indexOffset / 3 + vertexIndex - baseVertex;
    output.instance_index = absouluteInstanceIdx;
    
    return output;
}