#pragma once

namespace Visibility
{
    public VisibilityId Load(int2 pixel)
    {
        let scene = Bindless.GetGpuscene();
        const uint primitiveWordCount = scene.SoftMeshShader[0].PrimitiveWordCount;
        const uint2 physical =
            Bindless.GetViewStorageTexture<uint2>(Bindless.RT_MINIGBUFFER)[pixel];
        return DecodeVisibilityPhysical(physical, primitiveWordCount);
    }

    public int2 Dimensions()
    {
        return Bindless.GetViewStorageTextureDimensions<uint2>(Bindless.RT_MINIGBUFFER);
    }

    public bool IsValid(VisibilityId value)
    {
        return value.instanceIdx != 0;
    }
}
