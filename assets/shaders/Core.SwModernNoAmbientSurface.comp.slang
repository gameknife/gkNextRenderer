import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/PreProcessor.slang"
#include "common/GTAOUpsample.slang"
#include "common/NoAmbientShading.slang"

// SoftwareModernNoAmbient Core Shading on the Primary Surface path.
//
// No visibility decode, no get_material_data, no FetchGBuffer, no MRA sample: Core.SurfaceBuild
// already did all of that once, at full rate. This kernel only turns a resolved surface into
// lighting, so under checkerboard it is the only thing running at half rate and the only thing the
// resolve has to reconstruct.
//
// It does not write any surface-class RT. That is what lets the checkerboard resolve shrink to
// RT_SINGLE_DIFFUSE + RT_AMBIENT instead of neighbour-copying depth, normals, motion and object id.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    const uint viewBase = Bindless.GetGpuscene().CustomData0;
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const uint2 shadingPixel = Scheduler.ResolveAnalyticPixel(
        DTid.xy, Scheduler.IsCheckerboardActive(Bindless.GetGpuscene().CustomData2), Camera.TotalFrames);
    const int2 pixel = int2(shadingPixel);
    if (any(shadingPixel >= size))
    {
        return;
    }

    let OutDiffuse = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let OutAmbient = Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT);

    const Surface.FSurfaceShading s = Surface.LoadShading(viewBase, pixel);
    if (!Surface.IsValid(s.Depth) || s.MaterialId == Surface.kInvalidMaterialId)
    {
        const float2 uv = ((float2(pixel) + 0.5f) / float2(size)) * 2.0 - 1.0;
        const float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        const float4 rayDir4 = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
        OutDiffuse.Store(pixel, VisibleBackground(normalize(rayDir4.xyz)));
        OutAmbient.Store(pixel, float4(0));
        return;
    }

    FNoAmbientShadingInput shadingInput;
    shadingInput.Position = Surface.ReconstructPosition(
        Camera.ProjectionInverse, Camera.ModelViewInverse, pixel, size, s.Depth);
    shadingInput.Normal = s.Normal;
    shadingInput.Roughness = s.Roughness;
    shadingInput.Metalness = s.Metalness;
    shadingInput.Albedo = s.Albedo;
    shadingInput.MaterialId = s.MaterialId;
    // Sinking the GTAO upsample into this kernel removes a dependency but measured slower than
    // leaving it in the small compose kernel: the nine-tap bilateral costs far more inside an
    // already register-heavy shading kernel. Opt in with r.gtao.applyInCore.
    shadingInput.SkyOcclusion =
        Surface.IsGtaoAppliedInCore(Bindless.GetGpuscene().CustomData2)
            ? GTAOSkyOcclusion(Camera, pixel, size, s.Depth, s.Normal)
            : 1.0f;

    const FNoAmbientShadingOutput lighting = ShadeNoAmbientSurface(shadingInput, pixel);
    OutDiffuse.Store(pixel, lighting.Diffuse);
    OutAmbient.Store(pixel, lighting.Ambient);
}
