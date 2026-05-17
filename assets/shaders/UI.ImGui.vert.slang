struct PushConsts
{
    float2 scale;
    float2 translate;
    float4 rotation;
    uint hdrOutput;
    float hdrReferenceWhiteNit;
    float2 padding;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
    [[vk::location(3)]] float4 clipRect : TEXCOORD1;
    [[vk::location(4)]] uint textureIndex : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation float4 clipRect : TEXCOORD1;
    [[vk::location(3)]] nointerpolation uint textureIndex : TEXCOORD2;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    VertexOutput output;

    float2 position = input.position * pushConsts.scale + pushConsts.translate;
    position = float2(position.x * pushConsts.rotation.x + position.y * pushConsts.rotation.y,
                      position.x * pushConsts.rotation.z + position.y * pushConsts.rotation.w);

    output.position = float4(position, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.clipRect = input.clipRect;
    output.textureIndex = input.textureIndex;
    return output;
}
