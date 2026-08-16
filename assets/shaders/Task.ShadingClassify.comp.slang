import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.SurfaceBuffer;
import Shader.ShadingScheduler;

// Shading tile classification.
//
// One workgroup per 8x8 tile, one thread per pixel. Each thread decides which shading bucket its
// pixel belongs to, the workgroup ORs those decisions into per-bucket 64-bit membership masks, and
// a single thread appends one entry per non-empty bucket. That is at most three atomics per tile,
// as opposed to one per pixel, and it keeps each bucket's dispatch spatially coherent.
//
// Rate lives here too: under checkerboard the Standard bucket only claims the parity this frame
// shades, while Background and Emissive stay full rate because their kernels are nearly free. No
// shading kernel below this pass knows that pixel parity exists.
//
// GPUScene.ReservedAddress0 holds the scheduler buffer; CustomData1 holds its per-bucket tile
// capacity.

groupshared uint g_mask[Scheduler.kBucketCount * 2u];

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    const GPUScene scene = Bindless.GetGpuscene();
    const uint viewBase = scene.CustomData0;
    const uint maxTiles = scene.CustomData1;
    const uint2 size = Surface.Dimensions(viewBase);
    const uint2 tileCounts = Scheduler.TileCounts(size);
    const uint pixelInTile = GTid.y * Scheduler.kTileSize + GTid.x;

    if (pixelInTile < Scheduler.kBucketCount * 2u)
    {
        g_mask[pixelInTile] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 pixel = DTid.xy;
    if (all(pixel < size))
    {
        const int2 ipixel = int2(pixel);
        const float depth = Surface.LoadDepth(viewBase, ipixel);
        const uint2 bsdf = Surface.LoadBsdf(viewBase, ipixel);

        uint bucket = Scheduler.kBucketStandard;
        if (!Surface.IsValid(depth) || bsdf.x == Surface.kInvalidMaterialId)
        {
            bucket = Scheduler.kBucketBackground;
        }
        else if (Surface.HasFlag(Surface.UnpackFlags(bsdf.y), Surface.kFlagEmissive))
        {
            bucket = Scheduler.kBucketEmissive;
        }

        const bool checkerboard = (scene.CustomData2 & 1u) != 0u;
        const bool claimed = bucket != Scheduler.kBucketStandard ||
            Scheduler.IsActiveParity(pixel, checkerboard, scene.Camera[0].TotalFrames);
        if (claimed)
        {
            const uint half = pixelInTile < 32u ? 0u : 1u;
            InterlockedOr(g_mask[bucket * 2u + half], 1u << (pixelInTile & 31u));
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (pixelInTile != 0u)
    {
        return;
    }

    const uint tileIndex = Gid.y * tileCounts.x + Gid.x;
    for (uint bucket = 0u; bucket < Scheduler.kBucketCount; ++bucket)
    {
        const uint2 mask = uint2(g_mask[bucket * 2u], g_mask[bucket * 2u + 1u]);
        if ((mask.x | mask.y) != 0u)
        {
            Scheduler.AppendTile(scene.ReservedAddress0, bucket, maxTiles, tileIndex, mask);
        }
    }
}
