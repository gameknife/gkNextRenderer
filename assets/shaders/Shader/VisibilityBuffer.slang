import Shader.Bindless;
module VisibilityBuffer;

// Visibility has one logical identity regardless of its physical storage. Expanded primitive
// records use two uints; the raster result is split into R32_UINT instance and R16_UINT triangle
// planes. Keep all physical access here so rendering code never reintroduces packed bit fields.
namespace Visibility
{
    public uint2 MakeId(uint instanceIdx, uint triangleIdx)
    {
        return uint2(instanceIdx, triangleIdx);
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
        return id.x != 0u;
    }
}
