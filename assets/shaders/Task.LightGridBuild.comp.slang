import Shader.Bindless;
import Shader.PathTracing;
#include "common/PreProcessor.slang"

// Builds the cascaded world-space light grid, once per scene per frame.
//
// Cell-major gather rather than a light-major scatter: one thread owns one cell and walks the
// light list. That costs O(cells x lights) but needs no atomics, produces a bit-identical result
// every frame (which regression captures depend on), and yields the per-cell importance ordering
// that a scatter would have to sort for afterwards.

static const uint kCapacity = uint(GPU_SCENE_LIGHT_GRID_CAPACITY);

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint cellIndex = DTid.x;
    if (cellIndex >= uint(GPU_SCENE_LIGHT_GRID_CELLS))
    {
        return;
    }

    GPUScene gpuScene = Bindless.GetGpuscene();
    UniformBufferObject camera = gpuScene.Camera[0];
    const uint cascadeCount = Common.GetLightGridCascadeCount();
    const uint cascadeIndex = cellIndex / uint(GPU_SCENE_LIGHT_GRID_CASCADE_CELLS);

    // Cascades beyond the active count still need their counts zeroed: a stale count from a
    // previous configuration would hand shading code entries that no longer mean anything.
    if (cascadeIndex >= cascadeCount || camera.LightCount == 0 ||
        gpuScene.SoftMeshShader.Lights == 0)
    {
        Common.StoreLightGridCell(cellIndex, 0u);
        return;
    }

    const uint withinCascade = cellIndex % uint(GPU_SCENE_LIGHT_GRID_CASCADE_CELLS);
    const uint sliceStride = uint(GPU_SCENE_LIGHT_GRID_SIZE_XZ * GPU_SCENE_LIGHT_GRID_SIZE_XZ);
    const int3 coord = int3(int(withinCascade % uint(GPU_SCENE_LIGHT_GRID_SIZE_XZ)),
                            int(withinCascade / sliceStride),
                            int((withinCascade % sliceStride) / uint(GPU_SCENE_LIGHT_GRID_SIZE_XZ)));

    const float cellSize = Common.GetLightGridCellSize(cascadeIndex);
    const float halfExtent = cellSize * 0.5f;
    const float3 cellCenter =
        Common.GetLightGridOrigin(cascadeIndex) + (float3(coord) + 0.5f) * cellSize;

    // A cell whose whole extent a finer cascade already covers is never queried, because
    // SelectLightGridCascade always returns the finest containing cascade. Skipping the gather for
    // those cells keeps the coarse cascades cheap.
    //
    // Both corners must be inside. Testing only the centre leaves cells that straddle the finer
    // cascade's border empty, and every query landing in the straddling part resolves to *this*
    // cascade and finds no lights at all - an unlit band around the inner cascade.
    if (cascadeIndex > 0u &&
        Common.SelectLightGridCascade(cellCenter - halfExtent) < cascadeIndex &&
        Common.SelectLightGridCascade(cellCenter + halfExtent) < cascadeIndex)
    {
        Common.StoreLightGridCell(cellIndex, 0u);
        return;
    }

    const float cullThreshold = max(camera.LightGridCullThreshold, 0.0f);

    // Descending insertion into a fixed top-K. Only lights that clear the cull threshold reach the
    // insertion, so the inner cost is the distance test for the overwhelming majority of lights.
    float topWeight[GPU_SCENE_LIGHT_GRID_CAPACITY];
    uint topIndex[GPU_SCENE_LIGHT_GRID_CAPACITY];
    for (uint clearSlot = 0u; clearSlot < kCapacity; ++clearSlot)
    {
        topWeight[clearSlot] = 0.0f;
        topIndex[clearSlot] = 0u;
    }

    uint stored = 0u;
    for (uint lightIndex = 0u; lightIndex < camera.LightCount; ++lightIndex)
    {
        const float weight =
            Common.EvaluateLightCellWeight(gpuScene.Lights[lightIndex], cellCenter, halfExtent);
        // Beating the weakest kept entry is the single admission test: while the list is still
        // filling, that entry is a zero-weight placeholder, so any light above the cull threshold
        // gets in without needing a separate "not full yet" branch.
        if (weight <= cullThreshold || weight <= topWeight[kCapacity - 1u])
        {
            continue;
        }

        uint slot = kCapacity - 1u;
        while (slot > 0u && weight > topWeight[slot - 1u])
        {
            topWeight[slot] = topWeight[slot - 1u];
            topIndex[slot] = topIndex[slot - 1u];
            --slot;
        }
        topWeight[slot] = weight;
        topIndex[slot] = lightIndex;
        stored = min(stored + 1u, kCapacity);
    }

    float weightSum = 0.0f;
    for (uint sumSlot = 0u; sumSlot < stored; ++sumSlot)
    {
        weightSum += topWeight[sumSlot];
    }

    // CDF over the kept set only. Renormalising here is what makes a dropped light's selection pdf
    // exactly zero, which is the property the MIS weight relies on to stay unbiased once stage 2
    // routes NEE through this table.
    const float normalization = weightSum > 0.0f ? 1.0f / weightSum : 0.0f;
    float cdf = 0.0f;
    for (uint writeSlot = 0u; writeSlot < stored; ++writeSlot)
    {
        cdf += topWeight[writeSlot] * normalization;
        const float upperBound = (writeSlot + 1u == stored) ? 1.0f : cdf;
        Common.StoreLightGridEntry(cellIndex, writeSlot,
                                   Common.PackLightGridEntry(topIndex[writeSlot], upperBound));
    }
    Common.StoreLightGridCell(cellIndex, normalization > 0.0f ? stored : 0u);
}
