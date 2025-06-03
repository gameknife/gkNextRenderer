import Common;

// Texture bindings
[[vk::binding(0)]] RWTexture2D<float4> SourceImage;
[[vk::binding(1)]] RWTexture2D<float4> DestImage;
[[vk::binding(2)]] ConstantBuffer<UniformBufferObject> Camera;

struct PushConstant
{
    uint offset_x;
    uint offset_y;
};

[[vk::push_constant]] ConstantBuffer<PushConstant> pushConst;

// A simple accumulation shader, reproject can be implemented here later.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const int2 globalOffset = int2(Camera.ViewportRect.xy);
    const int2 ipos = int2(DTid.xy) + globalOffset;
    const int2 offset = int2(pushConst.offset_x, pushConst.offset_y);
    float4 outColor = SourceImage[ipos];
    if (Camera.HDR)
    {
        outColor = float4(LinearToST2084UE(outColor.rgb * Camera.PaperWhiteNit / 230.0), 1.0);
    }
    else
    {
        outColor = float4(Uncharted2_Tonemapping(outColor.rgb * Camera.PaperWhiteNit / 20000.0), 1.0);
    }
    DestImage[ipos + offset] = outColor;
}