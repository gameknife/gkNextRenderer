#ifndef GK_NEXT_SHADING_BUCKET_SLANG
#define GK_NEXT_SHADING_BUCKET_SLANG

// Entry prologue shared by every tile-list bucket kernel.
//
// A bucket kernel is dispatched indirectly with one workgroup per tile the classification assigned
// to it, so the workgroup id is a slot in that bucket's tile list rather than a screen position.
// This is the only place a bucket kernel is allowed to learn where it is: everything about rate,
// parity and tile packing stays inside the scheduler.
//
// Requires: Shader.SurfaceBuffer, Shader.ShadingScheduler and a gpuScene push constant.

bool ResolveBucketPixel(uint bucket, uint3 groupId, uint3 groupThreadId, out int2 outPixel)
{
    const GPUScene scene = Bindless.GetGpuscene();
    const uint2 size = Surface.Dimensions(scene.CustomData0);
    const uint2 tileCounts = Scheduler.TileCounts(size);
    const uint maxTiles = scene.CustomData1;
    const uint threadIndex = groupThreadId.y * Scheduler.kTileSize + groupThreadId.x;
    outPixel = int2(0, 0);

    // Standard is the only bucket that runs at the checkerboard rate. Testing a mask bit per lane
    // would leave half of them idle - the dispatch would be tile-shaped while the work is
    // half-density - so its tiles are packed two per workgroup and each lane looks up the n-th
    // shaded pixel instead. Background and Emissive are full rate and keep the direct mapping.
    const bool compactPairs = Scheduler.BucketCompactsPairs(bucket, scene.CustomData2);
    if (compactPairs)
    {
        uint slot;
        uint lane;
        if (!Scheduler.ResolveCompactedSlot(
                groupId.x, threadIndex,
                Scheduler.LoadTileCount(scene.ReservedAddress0, bucket), slot, lane))
        {
            return false;
        }
        const Scheduler.FTileEntry tile =
            Scheduler.LoadTile(scene.ReservedAddress0, bucket, maxTiles, slot);
        const uint pixelInTile = Scheduler.NthSetBit(tile.Mask, lane);
        if (pixelInTile >= Scheduler.kTilePixels)
        {
            return false;
        }
        outPixel = int2(Scheduler.TileOrigin(tile.TileIndex, tileCounts) +
                        uint2(pixelInTile % Scheduler.kTileSize, pixelInTile / Scheduler.kTileSize));
        return true;
    }

    const Scheduler.FTileEntry tile =
        Scheduler.LoadTile(scene.ReservedAddress0, bucket, maxTiles, groupId.x);
    outPixel = int2(Scheduler.TileOrigin(tile.TileIndex, tileCounts) + groupThreadId.xy);
    return Scheduler.TileHasPixel(tile.Mask, threadIndex);
}

#endif
