import Common;

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(1, 0)]]
StructuredBuffer<Material> Materials;

struct PushConsts
{
    float4x4 worldMatrix;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

struct VertexInput
{
    [[vk::location(0)]] half3 InPosition : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(mul(Camera.ViewProjection, pushConsts.worldMatrix), float4(input.InPosition, 1.0));
    output.position.z -= 0.000001f * output.position.w;
    return output;
}