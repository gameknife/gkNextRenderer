// General Bindless Texture

namespace Bindless
{
    // --- Screen-space RT slots (0..30): per-RenderView, resolved through ViewRT() ---

    // G-buffer / primary signals
    public static const int RT_PROGRESSIVE_DIFFUSE = 0;
    public static const int RT_SINGLE_DIFFUSE = 1;
    public static const int RT_MINIGBUFFER = 2;
    public static const int RT_OBJECTID_0 = 3;
    public static const int RT_OBJECTID_1 = 4;
    public static const int RT_MOTIONVECTOR = 5;
    public static const int RT_ALBEDO = 6;
    public static const int RT_NORMAL = 7;
    public static const int RT_SHADER_TIMER = 8;
    public static const int RT_SCENE_COLOR = 9;
    public static const int RT_PREV_DEPTHBUFFER = 10;
    public static const int RT_PROGRESSIVE_SPECULAR = 11;
    public static const int RT_SINGLE_SPECULAR = 12;
    public static const int RT_MOTIONMOMENT = 14;
    public static const int RT_DIFFUSE_HITDIST = 15;
    public static const int RT_SPECULAR_HITDIST = 16;
    public static const int RT_SPECULAR_ALBEDO = 17;

    public static const int RT_MINIGBUFFER_DRAW = 23;

    // Splat accumulation, ambient & AO
    public static const int RT_SPLAT_ACCUM = 26;
    public static const int RT_AMBIENT = 29;
    public static const int RT_GTAO = 30;

    // --- Global / shared slots: absolute, must NOT be remapped through ViewRT() ---
    public static const int RT_TEMP_USAGE0 = 50;
    public static const int RT_REMOTE_ENCODE0_Y = 60;
    public static const int RT_REMOTE_ENCODE0_UV = 61;
    public static const int RT_REMOTE_ENCODE1_Y = 62;
    public static const int RT_REMOTE_ENCODE1_UV = 63;
    public static const int RT_REMOTE_ENCODE2_Y = 64;
    public static const int RT_REMOTE_ENCODE2_UV = 65;
    public static const int RT_REMOTE_ENCODE3_Y = 66;
    public static const int RT_REMOTE_ENCODE3_UV = 67;
    
    public static const int RT_SWAPCHAIN0 = 100;
    public static const int RT_SWAPCHAIN1 = 101;
    public static const int RT_SWAPCHAIN2 = 102;

    public static const int RT_COUNT = 128;

    // --- Multi-viewport (RenderView) RT bank layout ---
    // Each RenderView owns a contiguous "bank" of screen-space RT slots so it can keep an
    // independent temporal history. Bank k occupies [k*kViewRtBankStride, k*kViewRtBankStride + RT_X].
    // Bank 0 == the legacy global layout above (primary view), so existing absolute slots are
    // unchanged and the primary view is bit-for-bit identical. The active view's bank base travels
    // in GPUScene.CustomData0; resolve a logical screen-space RT slot through ViewRT(base, RT_X).
    // Stride is sized to clear the screen-space range (0..~30) plus headroom while staying below the
    // global/shared slots (RT_TEMP_USAGE0=50, RT_REMOTE_*=60.., RT_SWAPCHAIN*=100..).
    public static const int kViewRtBankStride = 256;

    // Sun CSM cascade slots in ShadowMapArray (bindless binding 2).
    public static const int SM_SUN_CASCADE_0 = 0;
    public static const int SM_SUN_CASCADE_1 = 1;
    public static const int SM_SUN_CASCADE_2 = 2;
    public static const int SM_SUN_CASCADE_3 = 3;
    public static const int SM_SUN_CASCADE_COUNT = 4;
}

#ifndef __cplusplus
[[vk::binding(0, 0)]] __DynamicResource SampleTextureArray[];
[[vk::binding(1, 0)]] __DynamicResource StorageTextureArray[];
[[vk::binding(2, 0)]] __DynamicResource ShadowMapArray[];

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

    public uint2 GetStorageTextureDimensions<T : ITexelElement>(int index)
    {
        uint2 dim;
        StorageTextureArray[NonUniformResourceIndex(index)].as<RWTexture2D<T>>().GetDimensions(dim.x, dim.y);
        return dim;
    }

    // Resolve a logical screen-space RT slot into the active RenderView's bank.
    // viewBase comes from GPUScene.CustomData0 (0 for the primary view => absolute slot, no change).
    // Global / world-space / output slots (swapchain, remote encode, temp) must NOT be remapped:
    // pass those as absolute slots directly, not through ViewRT().
    public int ViewRT(uint viewBase, int rtSlot)
    {
        return int(viewBase) + rtSlot;
    }

    public Sampler2D GetShadowMap(int index)
    {
        return ShadowMapArray[NonUniformResourceIndex(index)].as<Sampler2D>();
    }
}
#endif
