import Bindless;
struct PushConsts
{
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

#define DRAW(IMAGE, quadx, quady, divider, ipos, isize, multipler) {                                                              \
        int2 uv = int2(0, 0);                                      \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                          \
            int2 ssize;                                            \
            IMAGE.GetDimensions(ssize.x, ssize.y);                 \
            float4 col = IMAGE[uv * ssize / isize]; \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = col * multipler;} \
}

#define DRAWNORMAL(IMAGE, quadx, quady, divider, ipos, isize) {                                                         \
        int2 uv = int2(0, 0);                                 \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                     \
            int2 ssize;                                       \
            IMAGE.GetDimensions(ssize.x, ssize.y);            \
            float4 col = IMAGE[uv * ssize / isize]; \
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

#define DRAWINTX(IMAGE, quadx, quady, divider, ipos, isize) {                                                         \
        int2 uv = int2(0, 0);                                 \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                     \
            int2 ssize;                                       \
            IMAGE.GetDimensions(ssize.x, ssize.y);            \
            uint icol = (IMAGE[uv * ssize / isize]  >> 17) & 0x7FFF;            \
            float3 color = UintToStableColor(icol);           \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);}                                                     \
    }

#define DRAWINTY(IMAGE, quadx, quady, divider, ipos, isize) {                                                          \
        int2 uv = int2(0, 0);                                  \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                      \
            int2 ssize;                                        \
            IMAGE.GetDimensions(ssize.x, ssize.y);             \
            uint icol = (IMAGE[uv * ssize / isize] & 0x1FFFF);        \
            float3 color = UintToStableColor(icol);           \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);}                                                     \
    }

#define DRAWDEPTH(IMAGE, quadx, quady, divider, ipos, isize, depth) {                                                               \
        int2 uv = int2(0, 0);                                       \
        if (DrawQuad(quadx, quady, divider, ipos, isize, uv)) {                                                           \
            int2 ssize;                                             \
            IMAGE.GetDimensions(ssize.x, ssize.y);                  \
            float icol = IMAGE[uv * ssize / isize];                 \
            float3 color = float3(pow(saturate(icol / depth), 100));           \
            Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(color, 1.0);}                                                     \
    }

bool DrawQuad(float quadx, float quady, float divider, int2 ipos, int2 isize, out int2 uv)
{
    float elementx = isize.x / divider;
    float elementy = isize.y / divider;

    if(ipos.x >= elementx * quadx + pushConsts.offset_x && ipos.x < elementx * (quadx + 1) + pushConsts.offset_x && ipos.y > elementy * quady + pushConsts.offset_y && ipos.y < elementy * (quady + 1) + pushConsts.offset_y)
    {
        uv = uint2((ipos - float2(elementx * quadx + pushConsts.offset_x, elementy * quady + pushConsts.offset_y)) * float2(divider, divider));
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
    int2 isize = Bindless.GetStorageTextureDimensions(pushConsts.SwapChainIdx);
    Bindless.GetStorageTexture<float4>(Bindless.RT_SWAPCHAIN0 + pushConsts.SwapChainIdx)[ipos] = float4(0, 0, 0, 1); // Clear the output image

    //ipos = int2(ipos.x, ipos.y);
    isize = int2(pushConsts.output_w, pushConsts.output_h);
    // line 1
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO), 0, 0, 4, ipos, isize, 1);
    DRAWNORMAL(Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL), 1, 0, 4, ipos, isize);
    DRAWDEPTH(Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER), 2, 0, 4, ipos, isize, 1.0f);
    DRAWINT(Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0), 3, 0, 4, ipos, isize);

// line 2/3 left
#ifdef SHADER_CLOCK
    //DRAW(ShaderTime, 0, 1, 4, ipos, isize, 1);
#endif
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR), 0, 2, 4, ipos, isize, 1);

    // line 2/3 right
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR), 3, 1, 4, ipos, isize, 0.005);
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE), 3, 2, 4, ipos, isize, 0.005);
    
    // center
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED), 0.5, 0.5, 2, ipos, isize, 1);

    // line back
    DRAW(Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE), 0, 3, 4, ipos, isize, 0.005);
    DRAWINTX(Bindless.GetStorageTexture<uint>(Bindless.RT_MINIGBUFFER), 1, 3, 4, ipos, isize);
    DRAWINTY(Bindless.GetStorageTexture<uint>(Bindless.RT_MINIGBUFFER), 2, 3, 4, ipos, isize);
}