// General Bindless Texture

#ifndef __cplusplus
implementing Common;
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
}

#ifndef __cplusplus
[[vk::binding(0, 0)]] __DynamicResource SampleTextureArray[];
[[vk::binding(1, 0)]] __DynamicResource StorageTextureArray[];
[[vk::push_constant]] ConstantBuffer<GPUScene> gpuScene;
namespace Bindless
{
    public GPUScene GetGpuscene()
    {
        return gpuScene;
    }

    public Sampler2D GetSampleTexture(int index )
    {
        return SampleTextureArray[index].as<Sampler2D>();
    }

    public RWTexture2D<T> GetStorageTexture<T : ITexelElement>(int index )
    {
        return StorageTextureArray[index].as<RWTexture2D<T>>();
    }

    public uint2 GetStorageTextureDimensions(int index)
    {
        uint2 dim;
        StorageTextureArray[index].as<RWTexture2D<float4>>().GetDimensions(dim.x, dim.y);
        return dim;
    }
}
#endif