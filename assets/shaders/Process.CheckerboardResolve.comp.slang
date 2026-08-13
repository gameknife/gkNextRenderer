import Shader.Bindless;
import Shader.GpuScene;
#include "common/CheckerboardRendering.slang"

static const uint RESOLVE_TRACING = 1u;
static const uint RESOLVE_SOFTWARE_MODERN_NO_AMBIENT = 2u;
static const uint RESOLVE_SCENE_COLOR = 3u;

void CopyTracingOutputs(int2 target, int2 source)
{
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[source];
    Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0)[target] =
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0)[source];
    Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[target] =
        Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR)[source];
    Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST)[target] =
        Bindless.GetViewStorageTexture<float>(Bindless.RT_DIFFUSE_HITDIST)[source];
    Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST)[target] =
        Bindless.GetViewStorageTexture<float>(Bindless.RT_SPECULAR_HITDIST)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SPECULAR_ALBEDO)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SPECULAR_ALBEDO)[source];
    Bindless.GetViewStorageTexture<uint2>(Bindless.RT_BSDF_DATA)[target] =
        Bindless.GetViewStorageTexture<uint2>(Bindless.RT_BSDF_DATA)[source];
}

void CopySoftwareModernNoAmbientOutputs(int2 target, int2 source)
{
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[source];
    Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0)[target] =
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0)[source];
    Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[target] =
        Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[source];
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR)[target] =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR)[source];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    const uint2 target = ResolveMissingCheckerboardPixel(dispatchThreadId.xy);
    if (any(target >= size))
    {
        return;
    }

    const int2 targetPixel = int2(target);
    const int2 sourcePixel = int2(FindCheckerboardSourcePixel(target, size));
    const uint resolveSet = Bindless.GetGpuscene().CustomData1;
    if (resolveSet == RESOLVE_TRACING)
    {
        CopyTracingOutputs(targetPixel, sourcePixel);
    }
    else if (resolveSet == RESOLVE_SOFTWARE_MODERN_NO_AMBIENT)
    {
        CopySoftwareModernNoAmbientOutputs(targetPixel, sourcePixel);
    }
    else if (resolveSet == RESOLVE_SCENE_COLOR)
    {
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[targetPixel] =
            Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[sourcePixel];
    }
}
