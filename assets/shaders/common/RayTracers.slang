implementing PathTracing;

#include "PreProcessor.slang"

// The four IRayTracer concrete strategies:
//   - FHiVoxelDDARayTracer  : page-indexed voxel DDA + cascaded fallback
//   - FAmbientCubeRayTracer : probe-only fake "trace" for cheap GI lookups
//   - FHardwareRayTracer    : RayQuery hardware ray tracing
//   - FSoftwareRayTracer    : screen-space + DDA hybrid
// IPrimaryRayCaster impls live in PrimaryRayCasters.slang; the IRayTracer /
// IDirectIlluminator / IRenderer interfaces in Interfaces.slang.

// Upper bound on DDA iterations for a single grid walk. The real budget is derived per ray from
// the clipped span; this only guards against degenerate configurations. A full diagonal across a
// CUBE_SIZE_XY^2 * CUBE_SIZE_Z cascade crosses 2 * 192 + 48 = 432 cell boundaries, so a walk that
// reaches this ceiling has already left anything the grid can represent.
static const uint kVoxelDDAMaxSteps = 768;

public struct FHiVoxelDDARayTracer : IRayTracer
{
    // Reseeds the DDA at `travelDistance` along the ray and returns the distance the rebuilt
    // state actually starts from. Every tMax produced afterwards is measured from that point,
    // so callers must keep adding it back: assigning a raw step result to the running travel
    // distance silently rebases the ray onto its own origin.
    float RebuildVoxelDDAState(float3 rayOrigin, float3 rayDir, float travelDistance, float cubeUnit, float3 cubeOffset,
                               out float3 voxelPos, out float3 tMax, out float3 tDelta, out float3 stepSign)
    {
        stepSign = sign(rayDir);
        tDelta = abs(cubeUnit / rayDir);

        float rebuildBias = max(cubeUnit * 0.01f, 0.0001f);
        float baseDistance = travelDistance + rebuildBias;
        float3 worldPos = rayOrigin + rayDir * baseDistance;
        voxelPos = (worldPos - cubeOffset) / cubeUnit;

        tMax = float3(1e30f, 1e30f, 1e30f);
        if (rayDir.x != 0) tMax.x = (stepSign.x > 0 ? ceil(voxelPos.x) - voxelPos.x : voxelPos.x - floor(voxelPos.x)) * tDelta.x;
        if (rayDir.y != 0) tMax.y = (stepSign.y > 0 ? ceil(voxelPos.y) - voxelPos.y : voxelPos.y - floor(voxelPos.y)) * tDelta.y;
        if (rayDir.z != 0) tMax.z = (stepSign.z > 0 ? ceil(voxelPos.z) - voxelPos.z : voxelPos.z - floor(voxelPos.z)) * tDelta.z;
        return baseDistance;
    }

    // World-space bounds of a cascade's traversable voxel volume. `inSolid` reports empty for
    // anything outside (0, CUBE_SIZE - 2], so the walk has to stop at that boundary instead of
    // spending its whole step budget on cells that can never produce a hit.
    void GetVoxelGridBounds(float cubeUnit, float3 cubeOffset, out float3 gridMin, out float3 gridMax)
    {
        gridMin = cubeOffset;
        gridMax = cubeOffset + float3(CUBE_SIZE_XY - 2, CUBE_SIZE_Z - 2, CUBE_SIZE_XY - 2) * cubeUnit;
    }

    // Slab clip. tEnter / tExit arrive pre-seeded with the caller's own range and are narrowed in
    // place; returns false when nothing survives, i.e. the ray never overlaps the box.
    bool ClipRayToBox(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax,
                      inout float tEnter, inout float tExit)
    {
        // An exactly-zero direction component would give 0 * inf = NaN in the slab test. The
        // nudge keeps it finite and stays far below one voxel over any usable trace distance.
        const float minComponent = 1e-6f;
        float3 safeDir;
        safeDir.x = abs(rayDir.x) < minComponent ? (rayDir.x < 0.0f ? -minComponent : minComponent) : rayDir.x;
        safeDir.y = abs(rayDir.y) < minComponent ? (rayDir.y < 0.0f ? -minComponent : minComponent) : rayDir.y;
        safeDir.z = abs(rayDir.z) < minComponent ? (rayDir.z < 0.0f ? -minComponent : minComponent) : rayDir.z;

        float3 t0 = (boxMin - rayOrigin) / safeDir;
        float3 t1 = (boxMax - rayOrigin) / safeDir;
        float3 tNear = min(t0, t1);
        float3 tFar = max(t0, t1);

        tEnter = max(tEnter, max(max(tNear.x, tNear.y), tNear.z));
        tExit = min(tExit, min(min(tFar.x, tFar.y), tFar.z));
        return tExit > tEnter;
    }

    // Finest cascade whose grid contains the point, falling back to the coarsest one. This runs once
    // per page for the primary voxel ray, so it goes through the single-pass grid resolve rather
    // than re-deriving each cascade's unit and offset from the UBO.
    void SelectVoxelCascade(float3 position, out float cubeUnit, out float3 cubeOffset, out uint baseOffset)
    {
        uint cascadeIndex;
        GetAmbientCubeGridAtPosition(position, cascadeIndex, cubeUnit, cubeOffset);
        baseOffset = GetCascadeBaseOffset(cascadeIndex);
    }

    float StepVoxelDDA(inout float3 voxelPos, inout float3 tMax, float3 tDelta, float3 stepSign, inout float3 normal)
    {
        normal = float3(0, 0, 0);
        if (tMax.x < tMax.y && tMax.x < tMax.z)
        {
            float stepDistance = tMax.x;
            voxelPos.x += stepSign.x;
            tMax.x += tDelta.x;
            normal.x = -stepSign.x;
            return stepDistance;
        }
        if (tMax.y < tMax.z)
        {
            float stepDistance = tMax.y;
            voxelPos.y += stepSign.y;
            tMax.y += tDelta.y;
            normal.y = -stepSign.y;
            return stepDistance;
        }

        float stepDistance = tMax.z;
        voxelPos.z += stepSign.z;
        tMax.z += tDelta.z;
        normal.z = -stepSign.z;
        return stepDistance;
    }

    // Occlusion is the same walk as TraceRayDDA, only the hit payload is thrown away. Keeping it
    // on one implementation is what gives shadow rays the distance-field skip and the grid clip;
    // the hand-rolled copy this replaced single-stepped every cell for the whole 80m range.
    public bool TraceOcclusionDDA(float3 rayOrigin, float3 rayDir, float maxDistance)
    {
        Vertex outVertex;
        NodeProxy outNode;
        return TraceRayDDA(rayOrigin, rayDir, maxDistance, float3(-1e5f), float3(1e5f), outVertex, outNode);
    }

    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        return TraceOcclusionDDA(rayOrigin, rayDir, 80.0f);
    }
    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        float rayLength = length(rayTarget - rayOrigin);
        Vertex outVertex;
        NodeProxy outNode;
        float4 tempColor;
        float hitDist = 0;
        return TraceRay(rayOrigin, (rayTarget - rayOrigin) / rayLength, rayLength - epsilon, outVertex, outNode, tempColor, hitDist);
    }
    public float3 GetAreaLightShadowOrigin(float3 position, float3 normal, float3 lightPosition,
                                           float minimumNormalOffset)
    {
        const float cubeUnit = max(GetAmbientCubeUnitAtPosition(position), 0.0001f);
        const float3 lightDirection = normalize(lightPosition - position);
        const float normalOffset = max(cubeUnit, minimumNormalOffset);
        return position + normalize(normal) * normalOffset + lightDirection * cubeUnit;
    }
    public bool TraceAreaLightSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return TraceSegment(rayOrigin, rayTarget, epsilon);
    }
    // Walks one cascade's voxel grid. The traversable span is the cascade volume intersected with
    // `boxMin`/`boxMax` (pass a huge box for an unbounded walk) and with `maxDistance`; callers
    // that only own part of a ray must pass the distance that is left, measured from `rayOrigin`.
    public bool TraceRayDDA(float3 rayOrigin, float3 rayDir, float maxDistance, float3 boxMin, float3 boxMax,
                            inout Vertex outVertex, inout NodeProxy outNode)
    {
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        float cubeUnit;
        float3 cubeOffset;
        uint baseOffset;
        SelectVoxelCascade(rayOrigin, cubeUnit, cubeOffset, baseOffset);

        // Clip to the cascade volume up front rather than testing the bounds every step. Both
        // boxes are convex, so a ray that leaves the span can never come back into it, and the
        // cells outside are empty by construction — stepping through them can only burn budget.
        float3 gridMin;
        float3 gridMax;
        GetVoxelGridBounds(cubeUnit, cubeOffset, gridMin, gridMax);
        gridMin = max(gridMin, boxMin - float3(cubeUnit));
        gridMax = min(gridMax, boxMax + float3(cubeUnit));

        float tEnter = 0.0f;
        float tExit = maxDistance;
        if (!ClipRayToBox(rayOrigin, rayDir, gridMin, gridMax, tEnter, tExit))
        {
            return false;
        }

        float3 voxelPos = float3(0);
        float3 tDelta = float3(0, 0, 0);
        float3 tMax = float3(0, 0, 0);
        float3 stepSign = float3(0, 0, 0);
        // ddaBase is the travel distance the current DDA state was seeded at. Every tMax the walk
        // yields is relative to it, so it has to be added back on each step — dropping it is what
        // made a second skip teleport the ray back towards its origin and lose the geometry ahead.
        float ddaBase = RebuildVoxelDDAState(rayOrigin, rayDir, tEnter, cubeUnit, cubeOffset,
                                             voxelPos, tMax, tDelta, stepSign);

        // A unit ray crosses at most sqrt(3) cell boundaries per cell of travel.
        uint maxSteps = min(uint((tExit - tEnter) / cubeUnit * 1.7321f) + 2u, kVoxelDDAMaxSteps);

        bool hit = false;
        uint hitMatId = 0;
        float3 normal = float3(0, 0, 0);
        float travelDistance = ddaBase;

        for (uint i = 0; i < maxSteps; ++i)
        {
            if (travelDistance > tExit)
            {
                break;
            }

            float offsetDist = 0;
            float clearanceVoxels = 1;
            if (inSolid(voxelPos, rayDir, offsetDist, hitMatId, clearanceVoxels, Voxels, baseOffset, cubeUnit))
            {
                hit = true;
                outVertex.Normal = dot(normal, normal) > 0.0f ? normalize(normal) : normalize(-rayDir);
                float3 voxelBoxMin = floor(voxelPos) * cubeUnit + cubeOffset - float3(0.01);
                float3 voxelBoxMax = floor(voxelPos) * cubeUnit + cubeOffset + float3(cubeUnit);
                outVertex.Position = Common.CalculateRayAABBEntry(rayOrigin, rayDir, voxelBoxMin, voxelBoxMax) + rayDir * offsetDist;
                outVertex.MaterialIndex = hitMatId;
                outVertex.TexCoord = float2(0.5, 0.5);
                break;
            }

            // clearanceVoxels is an L-infinity distance in cells (see RebuildDistanceField in
            // ProbeBaker.cpp), so every cell whose index differs from this one by less than that
            // on all three axes is empty. Travelling (clearance - 1) cells moves each axis index
            // by at most that much, whichever way the ray points, so the jump can never land past
            // a solid cell. A Euclidean field would only support clearance/sqrt(3) here, which is
            // why the old fixed 1.5-cell margin tunnelled through thin geometry.
            float safeSkipVoxels = clearanceVoxels - 1.0f;
            if (safeSkipVoxels >= 1.0f)
            {
                ddaBase = RebuildVoxelDDAState(rayOrigin, rayDir, travelDistance + safeSkipVoxels * cubeUnit,
                                               cubeUnit, cubeOffset, voxelPos, tMax, tDelta, stepSign);
                travelDistance = ddaBase;
                // The jump lands mid-cell, so there is no crossed face left to report as a normal.
                normal = float3(0, 0, 0);
                continue;
            }

            travelDistance = ddaBase + StepVoxelDDA(voxelPos, tMax, tDelta, stepSign, normal);
        }
        return hit;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist)
    {
        PageIndex* Pages = Bindless.GetGpuscene().Pages;
        float startUnit = max(GetAmbientCubeUnitAtPosition(rayOrigin), 0.0001f);
        // DDA voxel traversal
        bool hit = false;
        outHitDist = 0;
        rayOrigin = rayOrigin + rayDir * startUnit * 2;

        // Short rays never leave their own neighbourhood; the page index would cost more than it saves.
        if (maxDistance < PAGE_SIZE * 1.7)
        {
            return TraceRayDDA(rayOrigin, rayDir, maxDistance, float3(-1e5f), float3(1e5f), outVertex, outNode);
        }

        // Top-level page index. Pages are PAGE_COUNT^2 columns keyed on X/Z alone (FCPUPageIndex
        // maps a world position to pageZ * PAGE_COUNT + pageX and drops Y), so the walk must not
        // step vertically: a Y step lands on the same page and re-traces the column it just
        // rejected. Leaving the page box unbounded in Y instead lets one local trace cover it.
        float2 pagePos = float2(rayOrigin.x - PAGE_OFFSET.x, rayOrigin.z - PAGE_OFFSET.z) / PAGE_SIZE;
        float2 pageStepSign = float2(sign(rayDir.x), sign(rayDir.z));
        float2 pageDelta = abs(float2(PAGE_SIZE, PAGE_SIZE) / float2(rayDir.x, rayDir.z));
        float2 pageTMax = float2(1e30f, 1e30f);
        if (rayDir.x != 0) pageTMax.x = (pageStepSign.x > 0 ? ceil(pagePos.x) - pagePos.x : pagePos.x - floor(pagePos.x)) * pageDelta.x;
        if (rayDir.z != 0) pageTMax.y = (pageStepSign.y > 0 ? ceil(pagePos.y) - pagePos.y : pagePos.y - floor(pagePos.y)) * pageDelta.y;
        uint maxPageSteps = uint(maxDistance / PAGE_SIZE * 1.7) + 2u;

        for (uint i = 0; i < maxPageSteps; i++)
        {
            int2 pageCoord = int2(floor(pagePos));

            // Out-of-world columns are skipped rather than clamped: folding them onto a real page
            // (as the old index did) meant tracing whichever page happened to sit at the clamp.
            if (pageCoord.x >= 0 && pageCoord.x < PAGE_COUNT && pageCoord.y >= 0 && pageCoord.y < PAGE_COUNT)
            {
                uint pageIndex = uint(pageCoord.y) * uint(PAGE_COUNT) + uint(pageCoord.x);
                if (Pages[pageIndex].voxelCount > 0)
                {
                    float3 boxMin = float3(float(pageCoord.x) * PAGE_SIZE + PAGE_OFFSET.x + 0.01f, -1e5f,
                                           float(pageCoord.y) * PAGE_SIZE + PAGE_OFFSET.z + 0.01f);
                    float3 boxMax = float3(float(pageCoord.x) * PAGE_SIZE + PAGE_OFFSET.x + PAGE_SIZE, 1e5f,
                                           float(pageCoord.y) * PAGE_SIZE + PAGE_OFFSET.z + PAGE_SIZE);
                    float3 localStartPos = Common.CalculateRayAABBEntry(rayOrigin, rayDir, boxMin, boxMax);

                    // The local walk measures everything from the page entry, so the remaining
                    // budget has to be rebased with it. Handing it the full maxDistance let a hit
                    // past the end of the ray still count, which for TraceSegment means a light
                    // shadowed by geometry sitting behind it.
                    float pageEntryDistance = max(dot(localStartPos - rayOrigin, rayDir), 0.0f);
                    if (pageEntryDistance > maxDistance)
                    {
                        break;
                    }
                    if (TraceRayDDA(localStartPos, rayDir, maxDistance - pageEntryDistance, boxMin, boxMax,
                                    outVertex, outNode))
                    {
                        hit = true;
                        outHitDist = distance(outVertex.Position, rayOrigin);
                        break;
                    }
                }
            }

            // A ray with no horizontal component never leaves its column.
            if (pageStepSign.x == 0.0f && pageStepSign.y == 0.0f)
            {
                break;
            }

            if (pageTMax.x < pageTMax.y)
            {
                pagePos.x += pageStepSign.x;
                pageTMax.x += pageDelta.x;
            }
            else
            {
                pagePos.y += pageStepSign.y;
                pageTMax.y += pageDelta.y;
            }
        }
        return hit;
    }
}

public struct FAmbientCubeRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        return false;
    }

    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return false;
    }
    public float3 GetAreaLightShadowOrigin(float3 position, float3 normal, float3 lightPosition,
                                           float minimumNormalOffset)
    {
        return position;
    }
    public bool TraceAreaLightSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return false;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist)
    {
        float4 probeData = interpolateAmbientCubes<FullAmbientCubeSampler>(rayOrigin, rayDir);
        inoutRayColor.rgb = probeData.rgb;
        outHitDist = 100.0f; // Fake hit distance for ambient
        return probeData.a < 0.;
    }
}

// Indirect-only 版本：从 ambient cube probe 只读取间接反弹通道，跳过 `_D` 中的太阳直射部分。
// 配合 FShadowMapDirectIlluminator 使用，避免太阳光被 probe baked direct 和 shadow map 双重计算。
public struct FAmbientCubeRayTracerIndirect : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        return false;
    }

    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return false;
    }
    public float3 GetAreaLightShadowOrigin(float3 position, float3 normal, float3 lightPosition,
                                           float minimumNormalOffset)
    {
        return position;
    }
    public bool TraceAreaLightSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return false;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist)
    {
        float4 probeData = interpolateAmbientCubes<FullAmbientCubeSampler>(rayOrigin, rayDir);
        inoutRayColor.rgb = probeData.rgb;
        outHitDist = 100.0f;
        return probeData.a < 0.5;
    }
}
public bool IsPointOnAreaLight(float3 position, LightObject light, float planeTolerance)
{
    const float3 edge1 = light.p1.xyz - light.p0.xyz;
    const float3 edge3 = light.p3.xyz - light.p0.xyz;
    const float3 relative = position - light.p0.xyz;
    const float d00 = dot(edge1, edge1);
    const float d01 = dot(edge1, edge3);
    const float d11 = dot(edge3, edge3);
    const float d20 = dot(relative, edge1);
    const float d21 = dot(relative, edge3);
    const float denominator = d00 * d11 - d01 * d01;
    const float normalLengthSquared = dot(light.normal_area.xyz, light.normal_area.xyz);
    if (abs(denominator) <= EPS * EPS || normalLengthSquared <= EPS * EPS)
    {
        return false;
    }

    const float u = (d11 * d20 - d01 * d21) / denominator;
    const float v = (d00 * d21 - d01 * d20) / denominator;
    const float planeDistance = abs(dot(relative, light.normal_area.xyz)) * rsqrt(normalLengthSquared);
    return u >= -EPS && u <= 1.0f + EPS && v >= -EPS && v <= 1.0f + EPS &&
           planeDistance <= planeTolerance;
}

public struct FHardwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = PT_MAX_TRACE_DISTANCE;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Bindless.GetGpuscene().GetTLAS(),
            rayFlags,
            // Mask 0x01 contains only geometry that participates in sun shadows. The
            // ordinary 0xff mask remains used by primary and area-light visibility rays.
            0x01,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        float3 dir = rayTarget - rayOrigin;
        float len = length(dir);
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = epsilon;
        ray.TMax = len - epsilon;
        ray.Direction = dir / len;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Bindless.GetGpuscene().GetTLAS(),
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public float3 GetAreaLightShadowOrigin(float3 position, float3 normal, float3 lightPosition,
                                           float minimumNormalOffset)
    {
        return position + normalize(normal) * max(EPS2, minimumNormalOffset);
    }

    public bool TraceAreaLightSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        const float3 segment = rayTarget - rayOrigin;
        const float segmentLength = length(segment);
        // Visibility is evaluated on the open segment between the shading point and the sampled
        // emitter point.  A slightly wider endpoint margin absorbs ray/triangle rounding without
        // ever classifying an in-segment hit by material or projected position.
        const float endpointEpsilon = max(epsilon * 4.0f, segmentLength * 1.0e-5f);
        if (segmentLength <= epsilon + endpointEpsilon)
        {
            return false;
        }

        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = epsilon;
        ray.TMax = segmentLength - endpointEpsilon;
        ray.Direction = segment / segmentLength;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(Bindless.GetGpuscene().GetTLAS(), rayFlags, 0xff, ray);
        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = maxDistance;
        RayQuery<RAY_FLAG_NONE> q;
        let rayFlags = RAY_FLAG_NONE;

        q.TraceRayInline(
            Bindless.GetGpuscene().GetTLAS(),
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        outHitDist = 0;
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float t = q.CommittedRayT();
            outHitDist = t;

            float2 TwoBaryCoords = q.CommittedRayBarycentrics();
            float3 BaryCoords = float3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);

            float4x3 WorldToObject = q.CommittedRayWorldToObject();

            outNode = Bindless.GetGpuscene().Nodes[q.CommittedRayInstanceId()];
            outNode.instanceId = q.CommittedRayInstanceCustomIndex();
            const ModelData offsets = Bindless.GetGpuscene().Offsets[outNode.modelId];
            const uint indexOffset = offsets.indexOffset + q.CommittedPrimitiveIndex() * 3;
            const uint vertexOffset = offsets.vertexOffset;
            const Vertex v0 = UnpackVertex(Bindless.GetGpuscene().Vertices[vertexOffset + Bindless.GetGpuscene().Indices[indexOffset]]);
            const Vertex v1 = UnpackVertex(Bindless.GetGpuscene().Vertices[vertexOffset + Bindless.GetGpuscene().Indices[indexOffset + 1]]);
            const Vertex v2 = UnpackVertex(Bindless.GetGpuscene().Vertices[vertexOffset + Bindless.GetGpuscene().Indices[indexOffset + 2]]);
            outVertex.MaterialIndex = FetchMaterialId(outNode, v0.MaterialIndex);
            outVertex.Normal = normalize(mul(WorldToObject, Mix(v0.Normal, v1.Normal, v2.Normal, BaryCoords)).xyz);
            outVertex.TexCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, BaryCoords);
            outVertex.Position = rayOrigin + rayDir * t;// * (t - EPS2);
            //outVertex.Tangent = normalize(mul(WorldToObject, Mix(v0.Tangent, v1.Tangent, v2.Tangent, BaryCoords)).xyz);
            return true;
        }
        return false;
    }
}
public struct FSoftwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        float3 offsetPos = rayOrigin;

        if (DDARayTracer.TraceOcclusion(offsetPos, rayDir))
        {
            return true;
        }

        return false;
    }
    public bool TraceSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return DDARayTracer.TraceSegment(rayOrigin, rayTarget, epsilon);
    }
    public float3 GetAreaLightShadowOrigin(float3 position, float3 normal, float3 lightPosition,
                                           float minimumNormalOffset)
    {
        return DDARayTracer.GetAreaLightShadowOrigin(
            position, normal, lightPosition, minimumNormalOffset);
    }
    public bool TraceAreaLightSegment(float3 rayOrigin, float3 rayTarget, float epsilon)
    {
        return DDARayTracer.TraceAreaLightSegment(rayOrigin, rayTarget, epsilon);
    }
    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist)
    {
        if (maxDistance <= 0.0f)
        {
            outHitDist = 0.0f;
            return false;
        }

        float3 offsetPos = rayOrigin;
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float3 viewRayDir = mul(Camera.ModelView, float4(rayDir, 0.0f)).xyz;
        float clampedTraceDistance = min(maxDistance, FAST_MAX_TRACE_DISTANCE);

        // Near field tracing parameters
        const float longDistanceInit = 0.5; // Far field starting distance (to avoid self-intersection)
        const float depthToleration = 0.2;         // Near field depth toleration
        const bool allowScreenSpaceTrace = (viewRayDir.z < -0.05f);

        if (allowScreenSpaceTrace &&
            Common.TraceInScreenSpace(offsetPos, rayDir, min(longDistanceInit, clampedTraceDistance), depthToleration, outVertex.Position, outVertex.Normal, outVertex.MaterialIndex))
        {
            outVertex.TexCoord = float2(0.5, 0.5);
            outHitDist = distance(outVertex.Position, rayOrigin);
            return true;
        }

        // use dda directly
        // offsetPos = offsetPos + rayDir * longDistanceInit; no need to offset, dda tracing will handle it
        if (DDARayTracer.TraceRay(offsetPos, rayDir, clampedTraceDistance, outVertex, outNode, inoutRayColor, outHitDist))
        {
            return true;
        }

        outHitDist = 0;
        return false;
    }

    FHiVoxelDDARayTracer DDARayTracer;
}
