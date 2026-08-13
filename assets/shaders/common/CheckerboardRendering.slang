#ifndef CHECKERBOARD_RENDERING_SLANG
#define CHECKERBOARD_RENDERING_SLANG

// GPUScene.CustomData2 is pass-local. Shading passes use bit 0 to opt into the
// compact half-width dispatch; unrelated compute passes retain their existing semantics.
static const uint CHECKERBOARD_SHADING_FLAG = 1u << 0u;

uint2 ResolveShadingPixel(uint2 dispatchPixel)
{
    if ((Bindless.GetGpuscene().CustomData2 & CHECKERBOARD_SHADING_FLAG) == 0u)
    {
        return dispatchPixel;
    }

    const uint framePhase = Bindless.GetGpuscene().Camera[0].TotalFrames & 1u;
    return uint2(dispatchPixel.x * 2u + ((dispatchPixel.y + framePhase) & 1u),
                 dispatchPixel.y);
}

uint2 ResolveMissingCheckerboardPixel(uint2 dispatchPixel)
{
    const uint framePhase = Bindless.GetGpuscene().Camera[0].TotalFrames & 1u;
    const uint activeParity = (dispatchPixel.y + framePhase) & 1u;
    return uint2(dispatchPixel.x * 2u + (1u - activeParity), dispatchPixel.y);
}

uint2 FindCheckerboardSourcePixel(uint2 missingPixel, uint2 size)
{
    if (size.x == 1u)
    {
        const uint sourceY = missingPixel.y > 0u ? missingPixel.y - 1u : min(1u, size.y - 1u);
        return uint2(0u, sourceY);
    }

    const uint framePhase = Bindless.GetGpuscene().Camera[0].TotalFrames & 1u;
    const uint activeParity = (missingPixel.y + framePhase) & 1u;
    if (activeParity == 0u)
    {
        return uint2(missingPixel.x - 1u, missingPixel.y);
    }

    return uint2(missingPixel.x + 1u < size.x ? missingPixel.x + 1u : missingPixel.x - 1u,
                 missingPixel.y);
}

#endif
