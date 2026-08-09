implementing PathTracing;

#include "PreProcessor.slang"

// Cascaded world-space light grid: the shared answer to "which lights matter at this world
// position". Three consumers use it, and they must agree or energy drifts:
//   - forward shading iterates a cell's lights instead of the whole scene
//   - NEE / ReSTIR select one light from a cell (stage 2)
//   - MIS reverse lookup identifies which emitter a BSDF ray landed on (stage 2)
//
// The structure is world-space rather than a view-frustum cluster grid because path-tracer bounce
// points, SHARC cache updates and ambient-cube baking all sample lights far outside the camera
// frustum. Cascades are camera-anchored, so a position outside every cascade is normal, not an
// error: those queries fall back to the global CDF and stay correct, just unaccelerated.

namespace Common
{
    // Scene-wide CDF light selection, the fallback whenever the grid has no answer for a position
    // (reserved1 carries the cdf built in Scene::UpdateLights, reserved2 the matching pdf).
    public uint SelectLight(float lightSelector)
    {
        GPUScene gpuScene = Bindless.GetGpuscene();
        UniformBufferObject Camera = gpuScene.Camera[0];
        uint lightIndex = Camera.LightCount - 1;
        for (uint i = 0; i < Camera.LightCount; ++i)
        {
            if (lightSelector < asfloat(gpuScene.Lights[i].reserved1))
            {
                lightIndex = i;
                break;
            }
        }
        return lightIndex;
    }

    public float GetLightSampleMeasure(LightObject light)
    {
        if (light.lightType == LightTypePoint)
        {
            return 1.0f;
        }
        return light.lightType == LightTypeArea ? light.normal_area.w : 0.0f;
    }

    public static const uint LightGridInvalidCascade = 0xFFFFFFFFu;

    public uint GetLightGridCascadeCount()
    {
        return min(uint(Bindless.GetGpuscene().Camera[0].LightGridCascadeCount),
                   uint(GPU_SCENE_LIGHT_GRID_CASCADE_MAX));
    }

    public bool IsLightGridAvailable()
    {
        return GetLightGridCascadeCount() > 0u &&
               Bindless.GetGpuscene().SoftMeshShader.LightGrid != 0;
    }

    public float GetLightGridCellSize(uint cascadeIndex)
    {
        const float baseSize = max(Bindless.GetGpuscene().Camera[0].LightGridAnchor.w, 0.01f);
        return baseSize * pow(GPU_SCENE_LIGHT_GRID_CASCADE_RATIO, float(cascadeIndex));
    }

    // Cascade origin. Derived from the uploaded anchor rather than the live camera position so the
    // build pass and every query land on bit-identical cell boundaries; a half-cell disagreement
    // would silently shift which lights a shading point sees.
    public float3 GetLightGridOrigin(uint cascadeIndex)
    {
        const float cellSize = GetLightGridCellSize(cascadeIndex);
        const float3 anchor = Bindless.GetGpuscene().Camera[0].LightGridAnchor.xyz;
        const float3 extent = float3(GPU_SCENE_LIGHT_GRID_SIZE_XZ,
                                     GPU_SCENE_LIGHT_GRID_SIZE_Y,
                                     GPU_SCENE_LIGHT_GRID_SIZE_XZ) * cellSize;
        // Snap to the cell lattice so translating the camera slides the grid a whole cell at a
        // time. An unsnapped origin re-bins every light every frame, which flickers the forward
        // path and invalidates ReSTIR history continuously.
        return floor((anchor - extent * 0.5f) / cellSize) * cellSize;
    }

    public int3 GetLightGridCoord(float3 worldPosition, uint cascadeIndex)
    {
        const float cellSize = GetLightGridCellSize(cascadeIndex);
        return int3(floor((worldPosition - GetLightGridOrigin(cascadeIndex)) / cellSize));
    }

    bool IsInsideLightGrid(int3 coord)
    {
        return all(coord >= int3(0)) &&
               coord.x < GPU_SCENE_LIGHT_GRID_SIZE_XZ &&
               coord.y < GPU_SCENE_LIGHT_GRID_SIZE_Y &&
               coord.z < GPU_SCENE_LIGHT_GRID_SIZE_XZ;
    }

    // Finest cascade containing the position, or LightGridInvalidCascade when none does.
    public uint SelectLightGridCascade(float3 worldPosition)
    {
        const uint cascadeCount = GetLightGridCascadeCount();
        for (uint cascadeIndex = 0u; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            if (IsInsideLightGrid(GetLightGridCoord(worldPosition, cascadeIndex)))
            {
                return cascadeIndex;
            }
        }
        return LightGridInvalidCascade;
    }

    public uint GetLightGridCellIndex(int3 coord, uint cascadeIndex)
    {
        const uint withinCascade =
            uint(coord.x) +
            uint(coord.z) * uint(GPU_SCENE_LIGHT_GRID_SIZE_XZ) +
            uint(coord.y) * uint(GPU_SCENE_LIGHT_GRID_SIZE_XZ * GPU_SCENE_LIGHT_GRID_SIZE_XZ);
        return cascadeIndex * uint(GPU_SCENE_LIGHT_GRID_CASCADE_CELLS) + withinCascade;
    }

    uint* GetLightGridCounts()
    {
        return (uint*)Bindless.GetGpuscene().SoftMeshShader.LightGrid;
    }

    uint* GetLightGridEntries()
    {
        return (uint*)(Bindless.GetGpuscene().SoftMeshShader.LightGrid +
                       uint64_t(GPU_SCENE_LIGHT_GRID_ENTRY_OFFSET));
    }

    public uint PackLightGridEntry(uint lightIndex, float cdfUpperBound)
    {
        const uint quantized = uint(round(saturate(cdfUpperBound) * 65535.0f));
        return (lightIndex & 0xFFFFu) | (quantized << 16u);
    }

    public void StoreLightGridCell(uint cellIndex, uint count)
    {
        GetLightGridCounts()[cellIndex] = count;
    }

    public void StoreLightGridEntry(uint cellIndex, uint slot, uint entry)
    {
        GetLightGridEntries()[cellIndex * uint(GPU_SCENE_LIGHT_GRID_CAPACITY) + slot] = entry;
    }

    // A resolved list of the lights that matter at one world position. Count == 0 with
    // UsesGlobalFallback set means "the grid has no answer here": iterate every light instead.
    public struct FLightCell
    {
        public uint CellIndex;
        public uint Count;
        public bool UsesGlobalFallback;

        // Light index for a slot. Under the global fallback the slot *is* the light index, so a
        // caller can iterate both cases with one loop and never branch on the fallback itself.
        public uint GetLightIndex(uint slot)
        {
            if (UsesGlobalFallback)
            {
                return slot;
            }
            return GetLightGridEntries()[CellIndex * uint(GPU_SCENE_LIGHT_GRID_CAPACITY) + slot] &
                   0xFFFFu;
        }

        public float GetCdfUpperBound(uint slot)
        {
            if (UsesGlobalFallback)
            {
                return 0.0f;
            }
            const uint entry =
                GetLightGridEntries()[CellIndex * uint(GPU_SCENE_LIGHT_GRID_CAPACITY) + slot];
            return float(entry >> 16u) * (1.0f / 65535.0f);
        }

        // Discrete probability of the selection walk landing on this slot. Derived from the same
        // stored quantised bounds the walk compares against, so the pdf an estimator divides by is
        // exactly the pdf it sampled with - quantisation costs variance, never bias.
        public float GetSlotPdf(uint slot)
        {
            if (UsesGlobalFallback)
            {
                return max(asfloat(Bindless.GetGpuscene().Lights[slot].reserved2), 0.0f);
            }
            const float upper = GetCdfUpperBound(slot);
            const float lower = slot == 0u ? 0.0f : GetCdfUpperBound(slot - 1u);
            return max(upper - lower, 0.0f);
        }
    };

    // The whole light set, addressed as a cell. Lets a caller run one loop whether or not the grid
    // has an answer for a position.
    public FLightCell GetGlobalLightCell()
    {
        FLightCell cell;
        cell.CellIndex = 0u;
        cell.Count = Bindless.GetGpuscene().Camera[0].LightCount;
        cell.UsesGlobalFallback = true;
        return cell;
    }

    public FLightCell GetLightCell(float3 worldPosition)
    {
        FLightCell cell;
        cell.CellIndex = 0u;
        cell.Count = 0u;
        cell.UsesGlobalFallback = true;

        UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        if (!IsLightGridAvailable())
        {
            cell.Count = camera.LightCount;
            return cell;
        }

        const uint cascadeIndex = SelectLightGridCascade(worldPosition);
        if (cascadeIndex == LightGridInvalidCascade)
        {
            cell.Count = camera.LightCount;
            return cell;
        }

        cell.CellIndex = GetLightGridCellIndex(GetLightGridCoord(worldPosition, cascadeIndex),
                                               cascadeIndex);
        cell.Count = min(GetLightGridCounts()[cell.CellIndex],
                         uint(GPU_SCENE_LIGHT_GRID_CAPACITY));
        cell.UsesGlobalFallback = false;
        return cell;
    }

    // The one authority on how likely light sampling was to pick this light at this shading point.
    // Sampling, the MIS weight paired with a BSDF hit, and the emitter reverse lookup must all
    // read it. If any two disagree the estimator stops being unbiased, and the symptom is a
    // brightness shift rather than noise - so no amount of convergence hides it.
    //
    // Zero is a meaningful answer: the light was culled from this cell, sampling can never pick it
    // here, and PowerHeuristic(bsdfPdf, 0) == 1 hands its entire contribution to the BSDF strategy.
    public float GetLightSelectionPdf(uint lightIndex, float3 shadingPosition)
    {
        const FLightCell cell = GetLightCell(shadingPosition);
        if (cell.UsesGlobalFallback)
        {
            return max(asfloat(Bindless.GetGpuscene().Lights[lightIndex].reserved2), 0.0f);
        }
        for (uint slot = 0u; slot < cell.Count; ++slot)
        {
            if (cell.GetLightIndex(slot) == lightIndex)
            {
                return cell.GetSlotPdf(slot);
            }
        }
        return 0.0f;
    }

    // Cell-local discrete light selection. Replaces walking the scene-wide CDF, and the returned
    // pdf is what GetLightSelectionPdf reports for the same position, by construction.
    public bool SelectLightAt(float3 shadingPosition, float lightSelector,
                              out uint lightIndex, out float pdf)
    {
        lightIndex = 0u;
        pdf = 0.0f;
        const FLightCell cell = GetLightCell(shadingPosition);
        if (cell.Count == 0u)
        {
            return false;
        }
        if (cell.UsesGlobalFallback)
        {
            lightIndex = SelectLight(lightSelector);
            pdf = max(asfloat(Bindless.GetGpuscene().Lights[lightIndex].reserved2), 0.0f);
            return pdf > 0.0f;
        }

        // First slot whose upper bound exceeds the selector. A slot whose stored bounds coincide
        // has zero width and can never be chosen, which is what keeps a light that quantised to
        // zero probability out of the sample set and consistent with its reported pdf of zero.
        uint chosen = cell.Count - 1u;
        for (uint slot = 0u; slot < cell.Count; ++slot)
        {
            if (lightSelector < cell.GetCdfUpperBound(slot))
            {
                chosen = slot;
                break;
            }
        }
        lightIndex = cell.GetLightIndex(chosen);
        pdf = cell.GetSlotPdf(chosen);
        return pdf > 0.0f;
    }

    // Importance a light carries for a cell, shared by the build pass and any debug view. Distance
    // is measured to the closest point of the cell so the estimate is conservative everywhere
    // inside it, and clamped by the cell's half-diagonal so a light sitting in the cell does not
    // produce an unbounded weight.
    public float EvaluateLightCellWeight(LightObject light, float3 cellCenter, float cellHalfExtent)
    {
        const float sampleMeasure = GetLightSampleMeasure(light);
        if (sampleMeasure <= 0.0f)
        {
            return 0.0f;
        }
        Material lightMaterial = Bindless.GetGpuscene().GetMaterial(light.lightMatIdx);
        if (lightMaterial.MaterialModel != MaterialDiffuseLight)
        {
            return 0.0f;
        }
        const float luminance =
            dot(max(lightMaterial.Diffuse.rgb, float3(0.0f)), float3(0.2126f, 0.7152f, 0.0722f));
        if (luminance <= 0.0f)
        {
            return 0.0f;
        }

        // Bounding sphere of the emitter: a quad's centre plus half its diagonal, a point light's
        // optional proxy radius. Conservative on both counts.
        float3 lightCenter = light.p0.xyz;
        float lightRadius = max(light.p0.w, 0.0f);
        if (light.lightType != LightTypePoint)
        {
            const float3 edgeU = light.p1.xyz - light.p0.xyz;
            const float3 edgeV = light.p3.xyz - light.p0.xyz;
            lightCenter = light.p0.xyz + (edgeU + edgeV) * 0.5f;
            lightRadius = length(edgeU + edgeV) * 0.5f;
        }

        const float3 toCenter = lightCenter - cellCenter;
        // Closest point of the cell AABB to the emitter's bounding sphere.
        const float3 outside = max(abs(toCenter) - float3(cellHalfExtent), float3(0.0f));
        const float closestDistance = max(length(outside) - lightRadius, 0.0f);
        const float cellHalfDiagonal = cellHalfExtent * 1.7320508f;
        const float distanceSquared = max(closestDistance * closestDistance,
                                          cellHalfDiagonal * cellHalfDiagonal);
        return luminance * sampleMeasure / distanceSquared;
    }
}
