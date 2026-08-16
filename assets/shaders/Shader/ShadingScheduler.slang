import Shader.Bindless;
module ShadingScheduler;

// Shading Scheduler: the allocation layer between the Primary Surface and the shading kernels.
//
// It answers one question - "which pixels does this frame hand to which shading kernel?" - along
// two orthogonal axes:
//
//   bucket : one shading method. A bucket is a kernel boundary, so the count must stay in the
//            single digits. v1 buckets are Background, Emissive and Standard, which are simply the
//            early-out paths every Core Shading kernel already had, promoted into their own
//            dispatches so the Standard kernel stops carrying miss/emissive branches.
//   rate   : full or checkerboard. Rate is an allocation property; no kernel is allowed to know
//            about pixel parity. All parity knowledge in the surface path lives in this module.
//
// Classification is tile-granular (8x8), not per-pixel: a tile appends at most one entry per bucket
// and carries a 64-bit membership mask, which keeps locality and occupancy far better than a
// per-pixel atomic append would (the same conclusion UE5's shading bins and Call of Duty's tiled
// classification reached).
namespace Scheduler
{
    public static const uint kTileSize = 8u;
    public static const uint kTilePixels = kTileSize * kTileSize;

    public static const uint kBucketBackground = 0u;
    public static const uint kBucketEmissive = 1u;
    public static const uint kBucketStandard = 2u;
    public static const uint kBucketCount = 3u;

    // Buffer layout, in uints:
    //   [0 .. 3*kBucketCount)              per-bucket VkDispatchIndirectCommand {x, 1, 1}
    //   [3*kBucketCount .. 4*kBucketCount) per-bucket tile count
    //   [kHeaderUints + bucket*stride ..)  per-bucket tile list, kEntryUints per entry
    //
    // groupCountX is the append cursor during classification, so classification needs exactly one
    // atomic per tile per bucket. Task.ShadingClassifyFinalize then copies the final cursor into the
    // tile count and halves groupCountX for the compacted bucket. Both alternatives measured worse:
    // a second per-tile atomic costs more than the whole saving, and launching a group per tile and
    // retiring half of them gives the lane compaction back.
    public static const uint kHeaderUints = 4u * kBucketCount;
    public static const uint kEntryUints = 3u; // tileIndex, maskLow, maskHigh

    // GPUScene.CustomData2 bit 0. Checkerboard rendering only exists on the Primary Surface path,
    // so this is the one place a shading kernel is allowed to ask about rate.
    public bool IsCheckerboardActive(uint customData2)
    {
        return (customData2 & 1u) != 0u;
    }

    // Index of this bucket's VkDispatchIndirectCommand.groupCountX. Also the append cursor until
    // Task.ShadingClassifyFinalize rewrites it.
    public uint GroupCountIndex(uint bucket)
    {
        return bucket * 3u;
    }

    // Index of this bucket's tile count, published by Task.ShadingClassifyFinalize.
    public uint TileCountIndex(uint bucket)
    {
        return 3u * kBucketCount + bucket;
    }

    // Does this bucket pack two tiles per workgroup? Only Standard runs at the checkerboard rate,
    // so only it has half-empty tiles worth packing.
    public bool BucketCompactsPairs(uint bucket, uint customData2)
    {
        return bucket == kBucketStandard && IsCheckerboardActive(customData2);
    }

    // Turns the append cursor into a dispatch. Runs once, single-threaded per bucket, after every
    // classification workgroup has retired - which is the only point at which the tile count is
    // final and ceil(n/2) can be computed at all.
    public void FinalizeIndirect(uint64_t address, uint bucket, uint customData2)
    {
        uint* buffer = (uint*)address;
        const uint tileCount = buffer[GroupCountIndex(bucket)];
        buffer[TileCountIndex(bucket)] = tileCount;
        if (BucketCompactsPairs(bucket, customData2))
        {
            buffer[GroupCountIndex(bucket)] = (tileCount + 1u) / 2u;
        }
    }

    public uint ListBase(uint bucket, uint maxTiles)
    {
        return kHeaderUints + bucket * maxTiles * kEntryUints;
    }

    public uint2 TileCounts(uint2 size)
    {
        return (size + uint2(kTileSize - 1u)) / kTileSize;
    }

    public uint MaxTiles(uint2 size)
    {
        const uint2 tiles = TileCounts(size);
        return tiles.x * tiles.y;
    }

    public uint2 TileOrigin(uint tileIndex, uint2 tileCounts)
    {
        return uint2(tileIndex % tileCounts.x, tileIndex / tileCounts.x) * kTileSize;
    }

    // --- Classification side -------------------------------------------------------------------

    // Appends this tile to a bucket. Called once per tile per non-empty bucket, by a single thread:
    // three atomics per tile at worst, against one per pixel for a per-pixel append.
    public void AppendTile(uint64_t address, uint bucket, uint maxTiles, uint tileIndex, uint2 mask)
    {
        uint* buffer = (uint*)address;
        uint slot = 0u;
        InterlockedAdd(buffer[GroupCountIndex(bucket)], 1u, slot);
        if (slot >= maxTiles)
        {
            // Cannot happen: every tile appends at most once per bucket and the list is sized for
            // every tile. Guard anyway so a future rate/bucket change degrades into dropped tiles
            // instead of corrupting the neighbouring bucket's list.
            return;
        }
        const uint entry = ListBase(bucket, maxTiles) + slot * kEntryUints;
        buffer[entry + 0u] = tileIndex;
        buffer[entry + 1u] = mask.x;
        buffer[entry + 2u] = mask.y;
    }

    // --- Kernel side ---------------------------------------------------------------------------

    public struct FTileEntry
    {
        public uint TileIndex;
        public uint2 Mask;
    };

    public FTileEntry LoadTile(uint64_t address, uint bucket, uint maxTiles, uint slot)
    {
        uint* buffer = (uint*)address;
        const uint entry = ListBase(bucket, maxTiles) + slot * kEntryUints;
        FTileEntry result;
        result.TileIndex = buffer[entry + 0u];
        result.Mask = uint2(buffer[entry + 1u], buffer[entry + 2u]);
        return result;
    }

    public bool TileHasPixel(uint2 mask, uint pixelInTile)
    {
        return pixelInTile < 32u ? (mask.x & (1u << pixelInTile)) != 0u
                                 : (mask.y & (1u << (pixelInTile - 32u))) != 0u;
    }

    public uint LoadTileCount(uint64_t address, uint bucket)
    {
        uint* buffer = (uint*)address;
        return buffer[TileCountIndex(bucket)];
    }

    public uint TilePixelCount(uint2 mask)
    {
        return countbits(mask.x) + countbits(mask.y);
    }

    // Index of the n-th set bit of a 64-bit tile mask, or kTilePixels when there is no such bit.
    // This is what turns "half the lanes test a bit and return" into "every lane owns a pixel".
    public uint NthSetBit(uint2 mask, uint n)
    {
        const uint lowCount = countbits(mask.x);
        uint word = mask.x;
        uint remaining = n;
        uint base = 0u;
        if (n >= lowCount)
        {
            word = mask.y;
            remaining = n - lowCount;
            base = 32u;
        }
        for (uint bit = 0u; bit < 32u; ++bit)
        {
            if ((word & (1u << bit)) != 0u)
            {
                if (remaining == 0u)
                {
                    return base + bit;
                }
                --remaining;
            }
        }
        return kTilePixels;
    }

    // Compacted addressing: one workgroup covers two tiles, each served by half its lanes, so a
    // checkerboard tile's kTilePixels/2 shaded pixels fill 32 lanes completely instead of leaving
    // every other lane idle. The dispatch is ceil(tileCount/2) groups, so no group is launched for
    // nothing; only a trailing odd tile leaves its partner's lanes unused.
    public bool ResolveCompactedSlot(uint groupIndex, uint threadIndex, uint tileCount,
                                     out uint outSlot, out uint outLane)
    {
        outLane = threadIndex & 31u;
        outSlot = groupIndex * 2u + (threadIndex >> 5u);
        return outSlot < tileCount;
    }

    // --- Analytic allocation (scheduler off) ---------------------------------------------------

    // The zero-memory allocation the surface path uses when tile classification is disabled: a
    // single Standard bucket over the whole screen, with checkerboard expressed as a compacted
    // dispatch width. Same mapping as common/CheckerboardRendering.slang, which the renderers that
    // have not moved to the Primary Surface still use directly.
    public uint2 ResolveAnalyticPixel(uint2 dispatchPixel, bool checkerboardActive, uint totalFrames)
    {
        if (!checkerboardActive)
        {
            return dispatchPixel;
        }
        const uint framePhase = totalFrames & 1u;
        return uint2(dispatchPixel.x * 2u + ((dispatchPixel.y + framePhase) & 1u), dispatchPixel.y);
    }

    // Is this pixel the parity the checkerboard rate shades this frame?
    public bool IsActiveParity(uint2 pixel, bool checkerboardActive, uint totalFrames)
    {
        if (!checkerboardActive)
        {
            return true;
        }
        const uint framePhase = totalFrames & 1u;
        return (pixel.x & 1u) == ((pixel.y + framePhase) & 1u);
    }

    // Does this pass have to skip the parity that was not shaded? True only when the lighting is
    // handed to Native TAAU sparse; with a reconstruction pass in the chain every downstream pass
    // still sees a dense image.
    public bool ShouldSkipPixel(uint2 pixel, bool sparseLighting, bool checkerboardActive,
                                uint totalFrames)
    {
        return sparseLighting && !IsActiveParity(pixel, checkerboardActive, totalFrames);
    }

    // --- Reconstruction addressing ---------------------------------------------------------------

    // The pixel this frame did NOT shade, addressed from a half-width reconstruction dispatch.
    // Inverse of ResolveAnalyticPixel.
    public uint2 MissingPixel(uint2 dispatchPixel, uint totalFrames)
    {
        const uint framePhase = totalFrames & 1u;
        const uint activeParity = (dispatchPixel.y + framePhase) & 1u;
        return uint2(dispatchPixel.x * 2u + (1u - activeParity), dispatchPixel.y);
    }

    // Horizontal neighbour that was shaded this frame. Reconstruction only ever applies to lighting
    // channels, never to surface data - a copied depth or motion vector is wrong data, not
    // approximate data, which is why Core.SurfaceBuild runs at full rate.
    public uint2 ReconstructionSource(uint2 missingPixel, uint2 size, uint totalFrames)
    {
        if (size.x == 1u)
        {
            const uint sourceY = missingPixel.y > 0u ? missingPixel.y - 1u : min(1u, size.y - 1u);
            return uint2(0u, sourceY);
        }

        const uint framePhase = totalFrames & 1u;
        const uint activeParity = (missingPixel.y + framePhase) & 1u;
        if (activeParity == 0u)
        {
            return uint2(missingPixel.x - 1u, missingPixel.y);
        }
        return uint2(missingPixel.x + 1u < size.x ? missingPixel.x + 1u : missingPixel.x - 1u,
                     missingPixel.y);
    }
}
