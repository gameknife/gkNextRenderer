[[vk::binding(0)]] RWTexture2D<float4> OutImage;

[[vk::binding(0, 1)]] RWTexture2D<float4> FinalImage;
[[vk::binding(1, 1)]] RWTexture2D<float4> RenderImage;
[[vk::binding(2, 1)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(3, 1)]] RWTexture2D<uint> ObjectId0;
[[vk::binding(4, 1)]] RWTexture2D<uint> ObjectId1;
[[vk::binding(5, 1)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(6, 1)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(7, 1)]] RWTexture2D<float4> OutNormalBuffer;
[[vk::binding(10, 1)]] RWTexture2D<float> OutDepthBuffer;

float3 UintToStableColor(uint value)
{
    // 使用简单的hash函数确保颜色稳定
    uint hash = value;
    hash = hash * 1103515245u + 12345u;
    hash = (hash >> 16) ^ hash;
    hash = hash * 1103515245u + 12345u;

    // 提取三个分量
    uint r = (hash) & 0xFFu;
    uint g = (hash >> 8) & 0xFFu;
    uint b = (hash >> 16) & 0xFFu;

    // 转换为HSV空间以控制明度和彩度
    float hue = (r / 255.0) * 6.28318;          // 0-2π
    float saturation = 0.6 + (g / 255.0) * 0.4; // 0.6-1.0，保证有一定彩度
    float value = 0.5 + (b / 255.0) * 0.5;      // 0.5-1.0，保证不会太暗

    // HSV转RGB
    float c = value * saturation;
    float x = c * (1.0 - abs(fmod(hue / 1.047198, 2.0) - 1.0));
    float m = value - c;

    float3 rgb;
    if (hue < 1.047198) rgb = float3(c, x, 0);
    else if (hue < 2.094395) rgb = float3(x, c, 0);
    else if (hue < 3.141593) rgb = float3(0, c, x);
    else if (hue < 4.18879) rgb = float3(0, x, c);
    else if (hue < 5.235988) rgb = float3(x, 0, c);
    else rgb = float3(c, 0, x);

    return rgb + m;
}

#define DRAW(IMAGE, quadx, quady, divider, ipos, isize, multipler) { \
    int2 uv = int2(0, 0); \
    if(DrawQuad(quadx, quady, divider, ipos, isize, uv)){ \
    float4 col = IMAGE[uv]; \
    OutImage[ipos] = col * multipler;} \
}

#define DRAWNORMAL(IMAGE, quadx, quady, divider, ipos, isize) { \
    int2 uv = int2(0, 0); \
    if(DrawQuad(quadx, quady, divider, ipos, isize, uv)){ \
    float4 col = IMAGE[uv]; \
    OutImage[ipos] = col * 0.5 + 0.5;} \
}

#define DRAWINT(IMAGE, quadx, quady, divider, ipos, isize) { \
    int2 uv = int2(0, 0); \
    if(DrawQuad(quadx, quady, divider, ipos, isize, uv)){ \
    uint icol = IMAGE[uv]; \
    float3 color = UintToStableColor(icol); \
    OutImage[ipos] = float4(color, 1.0);} \
}

#define DRAWDEPTH(IMAGE, quadx, quady, divider, ipos, isize) {                                                         \
        int2 uv = int2(0, 0);                                 \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                     \
            float icol = IMAGE[uv];                            \
            float3 color = float3(saturate(icol / 1000.0f));           \
            OutImage[ipos] = float4(color, 1.0);}                                                     \
    }

bool DrawQuad(uint quadx, uint quady, uint divider, int2 ipos, int2 isize, out int2 uv)
{
    uint elementx = isize.x / divider;
    uint elementy = isize.y / divider;

    if(ipos.x >= elementx * quadx && ipos.x < elementx * (quadx + 1) && ipos.y > elementy * quady && ipos.y < elementy * (quady + 1))
    {
        uv = (ipos - int2(elementx * quadx, elementy * quady)) * int2(divider, divider);
        return true;
    }
    uv = int2(0, 0);
    return false;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 ipos = int2(DTid.xy);
    int2 isize;
    OutImage.GetDimensions(isize.x, isize.y);

    //OutImage[ipos] = OutImage[ipos];

    DRAW(RenderImage, 0u, 3u, 4u, ipos, isize, 1.0);
    DRAWNORMAL(OutNormalBuffer, 1u, 3u, 4u, ipos, isize);
    DRAWDEPTH(OutDepthBuffer, 2u, 3u, 4u, ipos, isize);
    DRAWINT(ObjectId0, 3u, 3u, 4u, ipos, isize);
}