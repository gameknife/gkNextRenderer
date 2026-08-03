// General Bindless Texture
//
// ============================================================================
// Bindless slot registry -- THE single source of truth for slot ownership
// ============================================================================
// Bindings 0 (COMBINED_IMAGE_SAMPLER) and 1 (STORAGE_IMAGE) are the flat 2D arrays, both sized
// RES_SLOT_COUNT. A slot number means the same thing in both, so one 2D resource can be readable
// at slot N through binding 0 and writable at slot N through binding 1.
//
// Bindings 3 and 4 are compact, volume-only arrays for 3D sampled/storage images. They use the
// same global RES_VOLUME_BASE slot range, but are indexed relative to that range. Keeping 2D and
// 3D resources in distinct descriptor bindings is intentional: Vulkan permits dimension aliases,
// but Metal translation of a shader containing both aliases is not reliable.
//
// The address space has two halves:
//
//   [0, RES_HIGH_BASE)              dynamically indexed, and it means something DIFFERENT per array:
//                                     binding 0 -> 2D scene textures, indexed by GlobalTexturePool
//                                                  texture index. Capacity RES_SCENE_TEXTURE_CAPACITY.
//                                     binding 1 -> 2D RenderView RT banks, bank k at k*kViewRtBankStride
//                                                  (8 banks x 256 = 2048; RT_* constants below).
//                                     binding 3 -> 3D volume sampled images, indexed from
//                                                  RES_VOLUME_BASE, capacity RES_VOLUME_COUNT.
//                                     binding 4 -> 3D volume storage images, indexed from
//                                                  RES_VOLUME_BASE, capacity RES_VOLUME_COUNT.
//                                   These arrays never collide because each resource dimension has
//                                   an explicit binding and volume indices are translated locally.
//
//   [RES_HIGH_BASE, RES_SLOT_COUNT) explicitly bound, statically partitioned, RES_* constants below.
//                                   Every range here is derived from the previous one, so the
//                                   partition cannot develop gaps or overlaps by hand-editing a
//                                   base. Add a range by inserting it in the chain, never by
//                                   picking a magic number.
//
// Owners bind through GlobalTexturePool::Bind{Sample,Storage}Texture using these constants; no
// module may invent its own base. The C++ view of this file static_asserts the partition.
//
// Resulting layout (RES_SCENE_TEXTURE_CAPACITY = 16384):
//
//   slot           count  binding  owner                           what
//   -------------  -----  -------  ------------------------------  ----------------------------
//   0..16383       16384  0        GlobalTexturePool               scene textures (dynamic)
//   k*256+0..127    2048  1        VulkanBaseRenderer              RT bank k (RT_* below)
//   16384..16391       8  0        OffscreenRenderViewController   secondary view outputs (3 used)
//   16392..16399       8  0+1      NextRemote RemoteServer         session composites (3 used)
//   16400..16447      48  1        NextRemote VideoPipeline        NV12 encode planes (3 x 16)
//   16448..16455       8  3+4      volume resources                3D froxel grids / LUTs
//   16456              1  0        AssetThumbnailRenderer          material preview
//   16457..16968     512  0        AssetThumbnailRenderer          mesh thumbnails
//   16969..17480     512  0        AssetThumbnailRenderer          material thumbnails
//   -> RES_SLOT_COUNT = 17481 descriptors in each 2D array (3D arrays have 8 descriptors)

namespace Bindless
{
    // --- Address space layout ---

    // Upper bound on concurrently registered scene textures. Exceeding it would run the texture
    // index into the explicitly-bound region below, so GlobalTexturePool::RegisterTexture throws
    // instead. Raise this (and RES_SLOT_COUNT follows) if a scene legitimately needs more.
    public static const int RES_SCENE_TEXTURE_CAPACITY = 16384;

    // Start of the explicitly-bound region. Must clear both dynamic regions:
    // scene textures (RES_SCENE_TEXTURE_CAPACITY) and RT banks (8 * kViewRtBankStride = 2048).
    public static const int RES_HIGH_BASE = RES_SCENE_TEXTURE_CAPACITY;

    // Offscreen RenderView sampled outputs (OffscreenRenderViewController).
    public static const int RES_VIEW_SAMPLE_BASE = RES_HIGH_BASE;
    public static const int RES_VIEW_SAMPLE_COUNT = 8;      // kMaxCameraSecondaryViews = 3, + headroom

    // NextRemote per-session composite outputs (RemoteServer). Bound in BOTH arrays.
    public static const int RES_REMOTE_COMPOSITE_BASE = RES_VIEW_SAMPLE_BASE + RES_VIEW_SAMPLE_COUNT;
    public static const int RES_REMOTE_COMPOSITE_COUNT = 8; // one per client view, 3 max today

    // NextRemote NV12 encode planes (VideoPipeline): 16 slots per client view.
    public static const int RES_REMOTE_ENCODE_BASE = RES_REMOTE_COMPOSITE_BASE + RES_REMOTE_COMPOSITE_COUNT;
    public static const int RES_REMOTE_ENCODE_STRIDE = 16;
    public static const int RES_REMOTE_ENCODE_COUNT = RES_REMOTE_ENCODE_STRIDE * 3;

    // Volume (3D) resources -- see the accessors further down.
    public static const int RES_VOLUME_BASE = RES_REMOTE_ENCODE_BASE + RES_REMOTE_ENCODE_COUNT;
    public static const int RES_VOLUME_COUNT = 8;

    public static const int RES_ATMOSPHERE_TRANSMITTANCE = RES_VOLUME_BASE + 0;
    public static const int RES_ATMOSPHERE_MULTI_SCATTER = RES_VOLUME_BASE + 1;
    public static const int RES_ATMOSPHERE_SKY_VIEW = RES_VOLUME_BASE + 2;
    public static const int RES_ATMOSPHERE_AERIAL_PERSPECTIVE = RES_VOLUME_BASE + 3;

    // Editor material preview (AssetThumbnailRenderer).
    public static const int RES_MATERIAL_PREVIEW = RES_VOLUME_BASE + RES_VOLUME_COUNT;

    // Editor thumbnail caches (AssetThumbnailRenderer): slot = base + asset index.
    public static const int RES_MESH_THUMBNAIL_BASE = RES_MATERIAL_PREVIEW + 1;
    public static const int RES_MESH_THUMBNAIL_COUNT = 512;
    public static const int RES_MATERIAL_THUMBNAIL_BASE = RES_MESH_THUMBNAIL_BASE + RES_MESH_THUMBNAIL_COUNT;
    public static const int RES_MATERIAL_THUMBNAIL_COUNT = 512;

    // Total capacity of each bindless array == end of the last range above.
    public static const int RES_SLOT_COUNT = RES_MATERIAL_THUMBNAIL_BASE + RES_MATERIAL_THUMBNAIL_COUNT;

    // --- Screen-space RT slots (0..30): per-RenderView, resolved through ViewRT() ---

    // G-buffer / primary signals
    // Single linear scene-color history used only by offline progressive rendering.
    public static const int RT_PROGRESSIVE_SCENE_COLOR = 0;
    public static const int RT_SINGLE_DIFFUSE = 1;
    public static const int RT_VISIBILITY_INSTANCE = 2;
    public static const int RT_OBJECTID_0 = 3;
    public static const int RT_OBJECTID_1 = 4;
    public static const int RT_MOTIONVECTOR = 5;
    public static const int RT_ALBEDO = 6;
    public static const int RT_NORMAL = 7;
    public static const int RT_SHADER_TIMER = 8;
    public static const int RT_SCENE_COLOR = 9;
    public static const int RT_PREV_DEPTHBUFFER = 10;
    // Slot 11 is intentionally unused; keep later slots stable for the bindless layout.
    public static const int RT_SINGLE_SPECULAR = 12;
    // material index + effective metalness for BSDF-aware ReSTIR reconstruction
    public static const int RT_BSDF_DATA = 13;
    public static const int RT_MOTIONMOMENT = 14;
    public static const int RT_DIFFUSE_HITDIST = 15;
    public static const int RT_SPECULAR_HITDIST = 16;
    public static const int RT_SPECULAR_ALBEDO = 17;
    public static const int RT_VISIBILITY_TRIANGLE = 18;

    public static const int RT_VISIBILITY_INSTANCE_DRAW = 23;
    public static const int RT_VISIBILITY_TRIANGLE_DRAW = 24;

    // Splat accumulation, ambient & AO
    public static const int RT_SPLAT_ACCUM = 26;
    public static const int RT_AMBIENT = 29;
    public static const int RT_GTAO = 30;

    // --- Global / shared slots: absolute, must NOT be remapped through ViewRT() ---
    public static const int RT_TEMP_USAGE0 = 50;
    // Per-swapchain-image ping/pong images used by the optional temporal post filter.
    // Slots 50..55 mirror RT_SWAPCHAIN0..2 and stay below the remote encode range.
    public static const int RT_TEMPORAL_POST_PING0 = RT_TEMP_USAGE0;
    public static const int RT_TEMPORAL_POST_PONG0 = RT_TEMPORAL_POST_PING0 + 3;
    // Full-resolution linear scene color targets consumed by the final output pass.
    // Slots 56..58 remain below the remote encode range.
    public static const int RT_TONEMAP_INPUT0 = RT_TEMPORAL_POST_PONG0 + 3;
    // Compatibility aliases for older shader and host code.
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
    // Total banks including bank 0. Owned here rather than by FBankAllocator so the address-space
    // assertions below can see it; FBankAllocator::kMaxConcurrentBanks forwards to this.
    public static const int kMaxViewRtBanks = 8;

    // Volume (3D) resources are global, absolute, and must NOT be remapped through ViewRT():
    // a froxel grid / 3D LUT is a world or camera-frustum resource shared by every RenderView,
    // so unlike the screen-space RTs above it does not live in a per-view bank.
    // Reserved for the unit test that guards the 3D bindless path end to end.
    public static const int RES_VOLUME_TEST = RES_VOLUME_BASE + RES_VOLUME_COUNT - 1;

    // Sun CSM cascade slots in ShadowMapArray (bindless binding 2).
    public static const int SM_SUN_CASCADE_0 = 0;
    public static const int SM_SUN_CASCADE_1 = 1;
    public static const int SM_SUN_CASCADE_2 = 2;
    public static const int SM_SUN_CASCADE_3 = 3;
    public static const int SM_SUN_CASCADE_COUNT = 4;

#ifdef __cplusplus
    // The high region is a derived chain, so these can only fire if someone hand-edits a base
    // back into a magic number or grows a range past its successor.
    static_assert(RT_COUNT <= kViewRtBankStride, "an RT bank must fit inside its stride");
    static_assert(kMaxViewRtBanks * kViewRtBankStride <= RES_HIGH_BASE,
                  "RT banks (binding 1) would run into the explicitly-bound region");
    static_assert(RES_SCENE_TEXTURE_CAPACITY <= RES_HIGH_BASE,
                  "scene textures (binding 0) would run into the explicitly-bound region");
    static_assert(RES_REMOTE_COMPOSITE_BASE >= RES_VIEW_SAMPLE_BASE + RES_VIEW_SAMPLE_COUNT, "range overlap");
    static_assert(RES_REMOTE_ENCODE_BASE >= RES_REMOTE_COMPOSITE_BASE + RES_REMOTE_COMPOSITE_COUNT, "range overlap");
    static_assert(RES_VOLUME_BASE >= RES_REMOTE_ENCODE_BASE + RES_REMOTE_ENCODE_COUNT, "range overlap");
    static_assert(RES_MATERIAL_PREVIEW >= RES_VOLUME_BASE + RES_VOLUME_COUNT, "range overlap");
    static_assert(RES_MESH_THUMBNAIL_BASE > RES_MATERIAL_PREVIEW, "range overlap");
    static_assert(RES_MATERIAL_THUMBNAIL_BASE >= RES_MESH_THUMBNAIL_BASE + RES_MESH_THUMBNAIL_COUNT,
                  "material thumbnails would overlap mesh thumbnails");
    static_assert(RES_SLOT_COUNT >= RES_MATERIAL_THUMBNAIL_BASE + RES_MATERIAL_THUMBNAIL_COUNT,
                  "array capacity must cover every registered range");
#endif
}

#ifndef __cplusplus
[[vk::binding(0, 0)]] __DynamicResource SampleTextureArray[];
[[vk::binding(1, 0)]] __DynamicResource StorageTextureArray[];
[[vk::binding(2, 0)]] __DynamicResource ShadowMapArray[];
[[vk::binding(3, 0)]] __DynamicResource VolumeSampleTextureArray[];
[[vk::binding(4, 0)]] __DynamicResource VolumeStorageTextureArray[];

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

    // --- Volume (3D) accessors ---
    // Volume slots are global (see RES_VOLUME_BASE), so callers pass absolute slot ids and these
    // accessors translate them to the compact, dimension-specific descriptor arrays. They never
    // pass through ViewRT().
    public Sampler3D GetSampleTexture3D(int index)
    {
        return VolumeSampleTextureArray[NonUniformResourceIndex(index - RES_VOLUME_BASE)].as<Sampler3D>();
    }

    public RWTexture3D<T> GetStorageTexture3D<T : ITexelElement>(int index)
    {
        return VolumeStorageTextureArray[NonUniformResourceIndex(index - RES_VOLUME_BASE)].as<RWTexture3D<T>>();
    }

    public uint3 GetStorageTexture3DDimensions<T : ITexelElement>(int index)
    {
        uint3 dim;
        VolumeStorageTextureArray[NonUniformResourceIndex(index - RES_VOLUME_BASE)].as<RWTexture3D<T>>().GetDimensions(dim.x, dim.y, dim.z);
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
