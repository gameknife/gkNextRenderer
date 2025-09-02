// General Bindless Texture

#ifndef __cplusplus
implementing Bindless;
#endif
namespace Bindless
{
    public static const int RT_ACCUMLATE_DIFFUSE = 0;
    public static const int RT_SINGLE_DIFFUSE = 1;
    public static const int RT_MINIGBUFFER = 2;
    public static const int RT_OBJEDCTID_0 = 3;
    public static const int RT_OBJEDCTID_1 = 4;
    public static const int RT_MOTIONVECTOR = 5;
    public static const int RT_ALBEDO = 6;
    public static const int RT_NORMAL = 7;
    public static const int RT_SHADER_TIMER = 8;

    public static const int RT_DENOISED = 9;
    
    public static const int RT_PREV_DEPTHBUFFER = 10;
    public static const int RT_ACCUMLATE_SPECULAR = 11;
    public static const int RT_SINGLE_SPECULAR = 12;

    public static const int RT_ACCUMLATE_ALBEDO = 13;

    public static const int RT_MOTIONMOMENT = 14;

    public static const int RT_SINGLE_PREV_DIFFUSE = 20;
    public static const int RT_SINGLE_PREV_SPECULAR = 21;
    public static const int RT_SINGLE_PREV_ALBEDO = 22;

    public static const int RT_TEMP_USAGE0 = 50;
    
    public static const int RT_SWAPCHAIN0 = 100;
    public static const int RT_SWAPCHAIN1 = 101;
    public static const int RT_SWAPCHAIN2 = 102;

    public static const int RT_COUNT = 128;
}

#ifndef __cplusplus
[[vk::binding(0, 0)]] __DynamicResource SampleTextureArray[];
[[vk::binding(1, 0)]] __DynamicResource StorageTextureArray[];

namespace Bindless
{
    public Sampler2D GetSampleTexture(int index )
    {
        return SampleTextureArray[NonUniformResourceIndex(index)].as<Sampler2D>();
    }

    public RWTexture2D<T> GetStorageTexture<T : ITexelElement>(int index )
    {
        return StorageTextureArray[NonUniformResourceIndex(index)].as<RWTexture2D<T>>();
    }

    public uint2 GetStorageTextureDimensions(int index)
    {
        uint2 dim;
        StorageTextureArray[index].as<RWTexture2D<float4>>().GetDimensions(dim.x, dim.y);
        return dim;
    }
}
#endif