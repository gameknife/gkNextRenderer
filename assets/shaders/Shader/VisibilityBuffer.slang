import Shader.Bindless;
module VisibilityBuffer;

// Visibility has one logical identity regardless of its physical storage. Expanded primitive
// records use two uints; the raster result is split into R32_UINT instance and R16_UINT triangle
// planes. Keep all physical access here so rendering code never reintroduces packed bit fields.
namespace Visibility
{
    // The instance word carries the one-based proxy id in its low bits and the LOD level the cull
    // pass selected in the high bits. Packing it here rather than in a side buffer is what makes
    // LOD correct for free across shadow slots and secondary render views: the level travels with
    // the sample, so a consumer decoding a pixel cannot read a level some other view has since
    // overwritten. MAX_RENDER_PROXIES is 131072, so the one-based id needs 18 bits and the R32_UINT
    // instance plane has room to spare.
    public static const uint kLodShift = 18;
    public static const uint kInstanceMask = (1u << kLodShift) - 1u;

    public uint2 MakeId(uint instanceIdx, uint triangleIdx)
    {
        return uint2(instanceIdx, triangleIdx);
    }

    public uint2 MakeIdWithLod(uint instanceIdx, uint triangleIdx, uint lod)
    {
        return uint2(instanceIdx | (lod << kLodShift), triangleIdx);
    }

    // Zero-based index into GPUScene::Nodes. Every consumer must go through this instead of
    // `id.x - 1`, which would fold the LOD bits into the proxy index.
    public uint ProxyIndex(uint2 id)
    {
        return (id.x & kInstanceMask) - 1u;
    }

    // Callers index their own ModelData with this: `model.lodIndexOffset[Visibility.LodLevel(id)]`.
    // The level cannot be resolved to an offset here, because BasicTypes.slang is #included rather
    // than imported and each module therefore owns a distinct ModelData type.
    // Level 0 reads back ModelData's own range, so an unsimplified proxy behaves exactly as before.
    public uint LodLevel(uint2 id)
    {
        return id.x >> kLodShift;
    }

    public uint2 LoadExpandedPrimitive(uint64_t address, uint primitiveIndex)
    {
        uint2* primitives = (uint2*)address;
        return primitives[primitiveIndex];
    }

    public void StoreExpandedPrimitive(uint64_t address, uint primitiveIndex, uint2 id)
    {
        uint2* primitives = (uint2*)address;
        primitives[primitiveIndex] = id;
    }

    public uint2 Load(int2 pixel, uint viewBase)
    {
        const uint instanceIdx =
            Bindless.GetStorageTexture<uint>(
                Bindless.ViewRT(viewBase, Bindless.RT_VISIBILITY_INSTANCE))[pixel];
        const uint triangleIdx =
            Bindless.GetStorageTexture<uint>(
                Bindless.ViewRT(viewBase, Bindless.RT_VISIBILITY_TRIANGLE))[pixel];
        return MakeId(instanceIdx, triangleIdx);
    }

    public uint2 Dimensions(uint viewBase)
    {
        return Bindless.GetStorageTextureDimensions<uint>(
            Bindless.ViewRT(viewBase, Bindless.RT_VISIBILITY_INSTANCE));
    }

    public bool IsValid(uint2 id)
    {
        return (id.x & kInstanceMask) != 0u;
    }
}
