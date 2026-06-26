// Remote play color conversion: swapchain (storage read) -> Y plane (R8_UNORM) + UV plane
// (R8G8_UNORM). The encoder source NV12 image is populated with vkCmdCopyImage on the graphics
// queue, so the full conversion path stays on GPU and avoids CPU readback.
import Bindless;

struct PushConsts
{
    uint swapChainIndex;
    uint outYIndex;
    uint outUvIndex;
    uint dstWidth;
    uint dstHeight;
    uint srcWidth;
    uint srcHeight;
    uint pad0;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

float3 FetchScaled(RWTexture2D<float4> src, uint dx, uint dy)
{
    uint sx = min(pushConsts.srcWidth - 1, (dx * pushConsts.srcWidth) / pushConsts.dstWidth);
    uint sy = min(pushConsts.srcHeight - 1, (dy * pushConsts.srcHeight) / pushConsts.dstHeight);
    return src[int2(sx, sy)].rgb;
}

float LumaNorm(float3 rgb)
{
    float y = 16.0 + 219.0 * dot(rgb, float3(0.2126, 0.7152, 0.0722));
    return clamp(round(y), 0.0, 255.0) / 255.0;
}

float2 ChromaNorm(float3 rgb00, float3 rgb10, float3 rgb01, float3 rgb11)
{
    float3 avg = (rgb00 + rgb10 + rgb01 + rgb11) * 0.25;
    float u = 128.0 + 224.0 * dot(avg, float3(-0.114572, -0.385428, 0.5));
    float v = 128.0 + 224.0 * dot(avg, float3(0.5, -0.454153, -0.045847));
    return float2(clamp(round(u), 0.0, 255.0), clamp(round(v), 0.0, 255.0)) / 255.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint dx = DTid.x;
    const uint dy = DTid.y;
    if (dx >= pushConsts.dstWidth || dy >= pushConsts.dstHeight)
    {
        return;
    }

    RWTexture2D<float4> src =
        Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.swapChainIndex);
    RWTexture2D<float> outY = Bindless.GetStorageTexture<float>(pushConsts.outYIndex);
    RWTexture2D<float2> outUv = Bindless.GetStorageTexture<float2>(pushConsts.outUvIndex);

    const float3 rgb = FetchScaled(src, dx, dy);
    outY[int2(dx, dy)] = LumaNorm(rgb);

    if (((dx | dy) & 1u) != 0u)
    {
        return;
    }

    const uint dx1 = min(pushConsts.dstWidth - 1, dx + 1);
    const uint dy1 = min(pushConsts.dstHeight - 1, dy + 1);
    outUv[int2(dx / 2u, dy / 2u)] = ChromaNorm(rgb, FetchScaled(src, dx1, dy), FetchScaled(src, dx, dy1),
                                                FetchScaled(src, dx1, dy1));
}
