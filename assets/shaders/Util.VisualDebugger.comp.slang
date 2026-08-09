import Shader.Bindless;
import Shader.VisibilityBuffer;
struct PushConsts
{
    // Unified push-constant header (offset 0..15): aliases gpuScene's header so
    // gpuScene.CustomData0 (active view RT bank base) is valid in this custom pass. Params follow.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    uint SwapChainIdx;
    uint offset_x;
    uint offset_y;
    uint output_w;
    uint output_h;
};

[[vk::push_constant]] ConstantBuffer<PushConsts> pushConsts;

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
    float saturation = 0.4 + (g / 255.0) * 0.4; // 0.4-0.8，保证有一定彩度
    float value = 0.7 + (b / 255.0) * 0.3;      // 0.7-1.0，保证不会太暗

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

float2 GetFilteredUv(float quadx, float quady, float divider, int2 ipos, int2 isize)
{
    const float2 tileSize = float2(isize) / divider;
    const float2 tileOrigin = tileSize * float2(quadx, quady) +
                              float2(pushConsts.offset_x, pushConsts.offset_y);
    // Map the destination pixel center to normalized source coordinates. The -0.5
    // in SampleBilinear below then applies the usual texel-center convention.
    return (float2(ipos) - tileOrigin + 0.5) / tileSize;
}

float4 SampleBilinear(RWTexture2D<float4> image, float2 uv)
{
    int2 size;
    image.GetDimensions(size.x, size.y);
    const int2 maxCoord = size - 1;
    const float2 position = clamp(uv * float2(size) - 0.5, 0.0, float2(maxCoord));
    const int2 base = int2(floor(position));
    const int2 nextX = min(base + int2(1, 0), maxCoord);
    const int2 nextY = min(base + int2(0, 1), maxCoord);
    const int2 nextXY = min(base + int2(1, 1), maxCoord);
    const float2 fraction = frac(position);

    const float4 top = lerp(image[base], image[nextX], fraction.x);
    const float4 bottom = lerp(image[nextY], image[nextXY], fraction.x);
    return lerp(top, bottom, fraction.y);
}

float SampleBilinear(RWTexture2D<float> image, float2 uv)
{
    int2 size;
    image.GetDimensions(size.x, size.y);
    const int2 maxCoord = size - 1;
    const float2 position = clamp(uv * float2(size) - 0.5, 0.0, float2(maxCoord));
    const int2 base = int2(floor(position));
    const int2 nextX = min(base + int2(1, 0), maxCoord);
    const int2 nextY = min(base + int2(0, 1), maxCoord);
    const int2 nextXY = min(base + int2(1, 1), maxCoord);
    const float2 fraction = frac(position);

    const float top = lerp(image[base], image[nextX], fraction.x);
    const float bottom = lerp(image[nextY], image[nextXY], fraction.x);
    return lerp(top, bottom, fraction.y);
}

#define DRAW(IMAGE, quadx, quady, divider, ipos, isize, multiplier) {                                                              \
        float2 uv = float2(0, 0);                                      \
        if (DrawQuadFiltered(quadx, quady, divider, ipos, isize, uv)) {                                                          \
            float4 col = SampleBilinear(IMAGE, uv); \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = col * multiplier;} \
}

#define DRAWNORMAL(IMAGE, quadx, quady, divider, ipos, isize) {                                                         \
        float2 uv = float2(0, 0);                                 \
        if (DrawQuadFiltered(quadx, quady, divider, ipos, isize, uv)) {                                                     \
            float4 col = SampleBilinear(IMAGE, uv); \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = col.rbga * 0.5 + 0.5;} \
}

#define DRAWINT(IMAGE, quadx, quady, divider, ipos, isize) {                                                         \
        int2 uv = int2(0, 0);                                 \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                     \
            int2 ssize;                                       \
            IMAGE.GetDimensions(ssize.x, ssize.y); \
    uint icol = IMAGE[uv * ssize / isize]; \
    float3 color = UintToStableColor(icol); \
    Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);} \
}

#define DRAWVISIBILITYINSTANCE(quadx, quady, divider, ipos, isize) {                                                  \
        int2 uv = int2(0, 0);                                 \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                     \
            int2 ssize = int2(Visibility.Dimensions(pushConsts._ViewHdr_CustomData0));        \
            uint icol = Visibility.Load(uv * ssize / isize, pushConsts._ViewHdr_CustomData0).x;       \
            float3 color = UintToStableColor(icol);           \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);}                                                     \
    }

#define DRAWVISIBILITYTRIANGLE(quadx, quady, divider, ipos, isize) {                                                  \
        int2 uv = int2(0, 0);                                  \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                      \
            int2 ssize = int2(Visibility.Dimensions(pushConsts._ViewHdr_CustomData0));         \
            uint icol = Visibility.Load(uv * ssize / isize, pushConsts._ViewHdr_CustomData0).y;  \
            float3 color = UintToStableColor(icol);           \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);}                                                     \
    }

#define DRAWDEPTH(IMAGE, quadx, quady, divider, ipos, isize, depth) {                                                               \
        float2 uv = float2(0, 0);                                       \
        if (DrawQuadFiltered(quadx, quady, divider, ipos, isize, uv)) {                                                           \
            float icol = SampleBilinear(IMAGE, uv);                 \
            float3 color = float3(pow(saturate(icol / depth), 100));           \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);}                                                     \
}

bool DrawQuadFiltered(float quadx, float quady, float divider, int2 ipos, int2 isize, out float2 uv)
{
    const float2 tileSize = float2(isize) / divider;
    const float2 tileOrigin = tileSize * float2(quadx, quady) +
                              float2(pushConsts.offset_x, pushConsts.offset_y);
    if (all(float2(ipos) >= tileOrigin) && all(float2(ipos) < tileOrigin + tileSize))
    {
        uv = GetFilteredUv(quadx, quady, divider, ipos, isize);
        return true;
    }
    uv = float2(0, 0);
    return false;
}

bool DrawQuad(float quadx, float quady, float divider, int2 ipos, int2 isize, out int2 uv)
{
    float elementX = isize.x / divider;
    float elementY = isize.y / divider;

    if(ipos.x >= elementX * quadx + pushConsts.offset_x && ipos.x < elementX * (quadx + 1) + pushConsts.offset_x && ipos.y > elementY * quady + pushConsts.offset_y && ipos.y < elementY * (quady + 1) + pushConsts.offset_y)
    {
        uv = uint2((ipos - float2(elementX * quadx + pushConsts.offset_x, elementY * quady + pushConsts.offset_y)) * float2(divider, divider));
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
    int2 isize = Bindless.GetStorageTextureDimensions<float4>(pushConsts.SwapChainIdx);
    // if (any(ipos >= isize))
    // {
    //     return;
    // }
    Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(0, 0, 0, 1); // Clear the output image

    //ipos = int2(ipos.x, ipos.y);
    isize = int2(pushConsts.output_w, pushConsts.output_h);
    // line 1
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_ALBEDO)), 0, 0, 4, ipos, isize, 1);
    DRAWNORMAL(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_NORMAL)), 1, 0, 4, ipos, isize);
    DRAWDEPTH(Bindless.GetStorageTexture<float>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_PREV_DEPTHBUFFER)), 2, 0, 4, ipos, isize, 1.0f);
    DRAWINT(Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_OBJECTID_0)), 3, 0, 4, ipos, isize);

// line 2/3 left
#ifdef SHADER_CLOCK
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SCENE_COLOR)), 0, 1, 4, ipos, isize, 1);
#endif
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_MOTIONVECTOR)), 0, 2, 4, ipos, isize, 100000);

    // line 2/3 right
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_SPECULAR)), 3, 1, 4, ipos, isize, 0.005);
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_DIFFUSE)), 3, 2, 4, ipos, isize, 0.005);
    
    // center
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SHADER_TIMER)), 0.5, 0.5, 2, ipos, isize, 1);

    // line back
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_DIFFUSE_HITDIST)), 0, 3, 4, ipos, isize, 0.005);
    DRAWVISIBILITYINSTANCE(1, 3, 4, ipos, isize);
    DRAWVISIBILITYTRIANGLE(2, 3, 4, ipos, isize);
    DRAWINT(Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_MOTIONMOMENT)), 3, 3, 4, ipos, isize);
}
