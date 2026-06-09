// Remote play color conversion: swapchain (storage read) -> planar I420 written to a
// host-visible buffer via buffer device address. Nearest-neighbor downscale + BT.709 limited.
// Each thread converts an 8x2 destination tile so every plane write is a 4-byte aligned store.
import Bindless;

struct PushConsts
{
    uint64_t outAddress;   // I420 planar buffer: Y (w*h), U (w/2*h/2), V (w/2*h/2)
    uint swapChainIndex;
    uint dstWidth;         // multiple of 8
    uint dstHeight;        // multiple of 2
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

uint LumaByte(float3 rgb)
{
    float y = 16.0 + 219.0 * dot(rgb, float3(0.2126, 0.7152, 0.0722));
    return (uint)clamp(round(y), 0.0, 255.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint dstW = pushConsts.dstWidth;
    uint dstH = pushConsts.dstHeight;
    uint tileX = DTid.x * 8;
    uint tileY = DTid.y * 2;
    if (tileX >= dstW || tileY >= dstH)
    {
        return;
    }

    RWTexture2D<float4> src =
        Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.swapChainIndex);

    float3 rgb[2][8];
    for (uint r = 0; r < 2; r++)
    {
        for (uint c = 0; c < 8; c++)
        {
            rgb[r][c] = FetchScaled(src, tileX + c, tileY + r);
        }
    }

    uint* dst = (uint*)pushConsts.outAddress;

    // Y plane: two rows, two packed-uint stores per row.
    for (uint r = 0; r < 2; r++)
    {
        uint rowBase = ((tileY + r) * dstW + tileX) / 4;
        for (uint w = 0; w < 2; w++)
        {
            uint packed = 0;
            for (uint b = 0; b < 4; b++)
            {
                packed |= LumaByte(rgb[r][w * 4 + b]) << (b * 8);
            }
            dst[rowBase + w] = packed;
        }
    }

    // Chroma: one 4-byte store per plane covering the tile's four 2x2 blocks.
    uint ySize = dstW * dstH;
    uint chromaW = dstW / 2;
    uint chromaBase = (tileY / 2) * chromaW + (tileX / 2);
    uint packedU = 0;
    uint packedV = 0;
    for (uint blockIdx = 0; blockIdx < 4; blockIdx++)
    {
        float3 avg = (rgb[0][blockIdx * 2] + rgb[0][blockIdx * 2 + 1] +
                      rgb[1][blockIdx * 2] + rgb[1][blockIdx * 2 + 1]) * 0.25;
        float u = 128.0 + 224.0 * dot(avg, float3(-0.114572, -0.385428, 0.5));
        float v = 128.0 + 224.0 * dot(avg, float3(0.5, -0.454153, -0.045847));
        packedU |= ((uint)clamp(round(u), 0.0, 255.0)) << (blockIdx * 8);
        packedV |= ((uint)clamp(round(v), 0.0, 255.0)) << (blockIdx * 8);
    }
    dst[(ySize + chromaBase) / 4] = packedU;
    dst[(ySize + ySize / 4 + chromaBase) / 4] = packedV;
}
