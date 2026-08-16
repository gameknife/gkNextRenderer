import Shader.Bindless;
import Shader.PathTracing;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;
#include "common/PreProcessor.slang"
#include "common/GTAOUpsample.slang"
#include "common/NoAmbientShading.slang"
#include "common/ShadingBucket.slang"

// Standard shading bucket for SoftwareModernNoAmbient.
//
// Dispatched indirectly over the tiles the classification assigned to Scheduler.kBucketStandard.
// It contains no miss branch and no emissive branch - those are separate kernels now - and it has
// no idea whether the frame is running at full or checkerboard rate.

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    const uint viewBase = Bindless.GetGpuscene().CustomData0;

    int2 pixel;
    if (!ResolveBucketPixel(Scheduler.kBucketStandard, Gid, GTid, pixel))
    {
        return;
    }

    const uint2 size = Surface.Dimensions(viewBase);
    const Surface.FSurfaceShading s = Surface.LoadShading(viewBase, pixel);

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

    const FNoAmbientShadingOutput lighting = ShadeNoAmbientStandard(shadingInput, pixel);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, lighting.Diffuse);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, lighting.Ambient);
}
