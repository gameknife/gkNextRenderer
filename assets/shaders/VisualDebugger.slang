[[vk::binding(0)]] RWTexture2D<float4> OutImage;
[[vk::binding(1)]] RWTexture2D<float4> InDebugImage1;
[[vk::binding(2)]] RWTexture2D<float4> InDebugImage2;
[[vk::binding(3)]] RWTexture2D<uint> InDebugImage3;
[[vk::binding(4)]] RWTexture2D<uint> InDebugImage4;

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
    OutImage[ipos] = float4(icol, icol, icol, icol) / 8.0;} \
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
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 ipos = int2(DTid.xy);
    int2 isize;
    OutImage.GetDimensions(isize.x, isize.y);

    OutImage[ipos] = OutImage[ipos];

    DRAW(InDebugImage1, 0u, 3u, 4u, ipos, isize, 1.0);
    DRAWNORMAL(InDebugImage2, 1u, 3u, 4u, ipos, isize);
    DRAWINT(InDebugImage4, 2u, 3u, 4u, ipos, isize);
    DRAWINT(InDebugImage3, 3u, 3u, 4u, ipos, isize);
}