#include "PreProcessor.slang"

// IRayTracer implementations split out of Shading.slang.
// Houses the four IRayTracer concrete strategies:
//   - FHiVoxelDDARayTracer  : page-indexed voxel DDA + cascaded fallback
//   - FAmbientCubeRayTracer : probe-only fake "trace" for cheap GI lookups
//   - FHardwareRayTracer    : RayQuery hardware ray tracing
//   - FSoftwareRayTracer    : screen-space + DDA hybrid
// IPrimaryRayCaster impls and the IRayTracer/IDirectIlluminator/IRenderer
// interfaces remain in Shading.slang.

public struct FHiVoxelDDARayTracer : IRayTracer
{
    void RebuildVoxelDDAState(float3 rayOrigin, float3 rayDir, float travelDistance, float cubeUnit, float3 cubeOffset,
                              out float3 voxelPos, out float3 tMax, out float3 tDelta, out float3 stepSign)
    {
        stepSign = sign(rayDir);
        tDelta = abs(cubeUnit / rayDir);

        float rebuildBias = max(cubeUnit * 0.01f, 0.0001f);
        float3 worldPos = rayOrigin + rayDir * (travelDistance + rebuildBias);
        voxelPos = (worldPos - cubeOffset) / cubeUnit;

        tMax = float3(1e30f, 1e30f, 1e30f);
        if (rayDir.x != 0) tMax.x = (stepSign.x > 0 ? ceil(voxelPos.x) - voxelPos.x : voxelPos.x - floor(voxelPos.x)) * tDelta.x;
        if (rayDir.y != 0) tMax.y = (stepSign.y > 0 ? ceil(voxelPos.y) - voxelPos.y : voxelPos.y - floor(voxelPos.y)) * tDelta.y;
        if (rayDir.z != 0) tMax.z = (stepSign.z > 0 ? ceil(voxelPos.z) - voxelPos.z : voxelPos.z - floor(voxelPos.z)) * tDelta.z;
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

    public bool TraceOcclusionDDA(float3 rayOrigin, float3 rayDir, float maxDistance)
    {
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
        uint cascadeCount = GetAmbientCubeCascadeCount();

        float cubeUnit = GetAmbientCubeUnit();
        float3 cubeOffset = GetAmbientCubeOffset();
        uint baseOffset = 0;
        float3 voxelPos = float3(0);
        bool foundCascade = false;

        for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            float candidateUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
            float3 candidateOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
            float3 candidateVoxelPos = (rayOrigin.xyz - candidateOffset) / candidateUnit;

            if (candidateVoxelPos.x >= 0 && candidateVoxelPos.y >= 0 && candidateVoxelPos.z >= 0 &&
                candidateVoxelPos.x <= CUBE_SIZE_XY - 1 && candidateVoxelPos.y <= CUBE_SIZE_Z - 1 &&
                candidateVoxelPos.z <= CUBE_SIZE_XY - 1)
            {
                cubeUnit = candidateUnit;
                cubeOffset = candidateOffset;
                baseOffset = GetCascadeBaseOffset(cascadeIndex);
                voxelPos = candidateVoxelPos;
                foundCascade = true;
                break;
            }
        }

        if (!foundCascade)
        {
            uint fallbackCascade = cascadeCount - 1;
            cubeUnit = GetAmbientCubeUnitAtCascade(fallbackCascade);
            cubeOffset = GetAmbientCubeOffsetAtCascade(fallbackCascade);
            baseOffset = GetCascadeBaseOffset(fallbackCascade);
            voxelPos = (rayOrigin.xyz - cubeOffset) / cubeUnit;
        }

        float3 tDelta = float3(0, 0, 0);
        float3 tMax = float3(0, 0, 0);
        float3 stepSign = float3(0, 0, 0);
        RebuildVoxelDDAState(rayOrigin, rayDir, 0.0f, cubeUnit, cubeOffset, voxelPos, tMax, tDelta, stepSign);

        uint maxSteps = max(uint(maxDistance / cubeUnit), 1u);
        float3 normal = float3(0, 0, 0);
        float travelDistance = 0.0f;

        for (uint i = 0; i < maxSteps; ++i)
        {
            if (travelDistance > maxDistance)
            {
                break;
            }

            float offsetDist = 0.0f;
            uint hitMatId = 0u;
            float skipLength = 1.0f;
            if (inSolid(voxelPos, rayDir, offsetDist, hitMatId, skipLength, Voxels, baseOffset, cubeUnit))
            {
                return true;
            }

            float minT = min(tMax.x, min(tMax.y, tMax.z));
            float tieEpsilon = max(cubeUnit * 0.001f, 0.0001f);
            normal = float3(0, 0, 0);
            if (stepSign.x != 0.0f && abs(tMax.x - minT) <= tieEpsilon)
            {
                voxelPos.x += stepSign.x;
                tMax.x += tDelta.x;
                normal.x = -stepSign.x;
            }
            if (stepSign.y != 0.0f && abs(tMax.y - minT) <= tieEpsilon)
            {
                voxelPos.y += stepSign.y;
                tMax.y += tDelta.y;
                normal.y = -stepSign.y;
            }
            if (stepSign.z != 0.0f && abs(tMax.z - minT) <= tieEpsilon)
            {
                voxelPos.z += stepSign.z;
                tMax.z += tDelta.z;
                normal.z = -stepSign.z;
            }
            travelDistance = minT;
        }

        return false;
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
    public bool TraceRayDDA(float3 rayOrigin, float3 rayDir, float maxDistance, float3 boxMin, float3 boxMax, bool useBounds, inout Vertex outVertex, inout NodeProxy outNode)
    {
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
        uint cascadeCount = GetAmbientCubeCascadeCount();

        float cubeUnit = GetAmbientCubeUnit();
        float3 cubeOffset = GetAmbientCubeOffset();
        uint baseOffset = 0;
        float3 voxelPos = float3(0);
        bool foundCascade = false;

        for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            float candidateUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
            float3 candidateOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
            float3 candidateVoxelPos = (rayOrigin.xyz - candidateOffset) / candidateUnit;

            if (candidateVoxelPos.x >= 0 && candidateVoxelPos.y >= 0 && candidateVoxelPos.z >= 0 &&
                candidateVoxelPos.x <= CUBE_SIZE_XY - 1 && candidateVoxelPos.y <= CUBE_SIZE_Z - 1 &&
                candidateVoxelPos.z <= CUBE_SIZE_XY - 1)
            {
                cubeUnit = candidateUnit;
                cubeOffset = candidateOffset;
                baseOffset = GetCascadeBaseOffset(cascadeIndex);
                voxelPos = candidateVoxelPos;
                foundCascade = true;
                break;
            }
        }

        if (!foundCascade)
        {
            uint fallbackCascade = cascadeCount - 1;
            cubeUnit = GetAmbientCubeUnitAtCascade(fallbackCascade);
            cubeOffset = GetAmbientCubeOffsetAtCascade(fallbackCascade);
            baseOffset = GetCascadeBaseOffset(fallbackCascade);
            voxelPos = (rayOrigin.xyz - cubeOffset) / cubeUnit;
        }


        uint hitMatId = 0;
        uint maxSteps = useBounds ? uint(16 * 4 * 1.8 * 2) : uint(maxDistance / cubeUnit);
        float3 tDelta = float3(0, 0, 0);
        float3 tMax = float3(0, 0, 0);
        float3 stepSign = float3(0, 0, 0);
        RebuildVoxelDDAState(rayOrigin, rayDir, 0.0f, cubeUnit, cubeOffset, voxelPos, tMax, tDelta, stepSign);

        bool hit = false;
        float3 normal = float3(0, 0, 0);
        float travelDistance = 0.0f;
        const float distanceFieldSkipSafety = 1.5f;

        for (uint i = 0; i < maxSteps; ++i)
        {
            if (!useBounds && travelDistance > maxDistance)
            {
                break;
            }

            if (useBounds)
            {
                float3 worldPos = rayOrigin + rayDir * travelDistance;
                if (any(worldPos < boxMin - float3(cubeUnit)) || any(worldPos > boxMax + float3(cubeUnit)))
                    break;
            }

            float offsetDist = 0;
            float skipLength = 1;
            if (inSolid(voxelPos, rayDir, offsetDist, hitMatId, skipLength, Voxels, baseOffset, cubeUnit))
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

            float safeSkipVoxels = max(skipLength - distanceFieldSkipSafety, 0.0f);
            if (safeSkipVoxels >= 1.0f)
            {
                float skipDistance = safeSkipVoxels * cubeUnit;
                travelDistance += skipDistance;
                RebuildVoxelDDAState(rayOrigin, rayDir, travelDistance, cubeUnit, cubeOffset, voxelPos, tMax, tDelta, stepSign);
                i += uint(floor(safeSkipVoxels));
                continue;
            }

            travelDistance = StepVoxelDDA(voxelPos, tMax, tDelta, stepSign, normal);
        }
        return hit;
    }

    // Fallback path used by TraceRay when the requested range is shorter than the page-trace
    // minimum (PAGE_SIZE * 1.7) — walks DDA directly without the top-level page index.
    public bool LegacyTraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode)
    {
        return TraceRayDDA(rayOrigin, rayDir, maxDistance, float3(0), float3(0), false, outVertex, outNode);
    }

    // Bounded DDA trace restricted to a single page's AABB; called per-page from TraceRay.
    public bool LocalTraceRay(float3 rayOrigin, float3 rayDir, float3 boxMin, float3 boxMax, inout Vertex outVertex, inout NodeProxy outNode)
    {
        return TraceRayDDA(rayOrigin, rayDir, 0, boxMin, boxMax, true, outVertex, outNode);
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float maxDistance, inout Vertex outVertex, inout NodeProxy outNode, inout float4 inoutRayColor, out float outHitDist)
    {
        PageIndex* Pages = Bindless.GetGpuscene().Pages;
        float startUnit = max(GetAmbientCubeUnitAtPosition(rayOrigin), 0.0001f);
        // DDA voxel traversal
        bool hit = false;
        outHitDist = 0;
        rayOrigin = rayOrigin + rayDir * startUnit * 2;

        // Top Level Page Index
        float3 pagePos = (rayOrigin.xyz - PAGE_OFFSET) / PAGE_SIZE;
        float3 pagetDelta = abs(PAGE_SIZE / rayDir);
        float3 pageTMax = float3(0, 0, 0);

        uint pageIndexStartPoint = Common.GetPageIndex(pagePos);
        if (maxDistance < PAGE_SIZE * 1.7)
        {
            return LegacyTraceRay(rayOrigin, rayDir, maxDistance, outVertex, outNode);
        }

        float3 stepSign = sign(rayDir);
        if (rayDir.x != 0) pageTMax.x = (stepSign.x > 0 ? ceil(pagePos.x) - pagePos.x : pagePos.x - floor(pagePos.x)) * pagetDelta.x;
        if (rayDir.y != 0) pageTMax.y = (stepSign.y > 0 ? ceil(pagePos.y) - pagePos.y : pagePos.y - floor(pagePos.y)) * pagetDelta.y;
        if (rayDir.z != 0) pageTMax.z = (stepSign.z > 0 ? ceil(pagePos.z) - pagePos.z : pagePos.z - floor(pagePos.z)) * pagetDelta.z;
        uint maxPageSteps = uint(maxDistance / PAGE_SIZE * 1.7);

        for (uint i = 0; i < maxPageSteps; i++)
        {
            float3 boxMin = floor(pagePos) * PAGE_SIZE + PAGE_OFFSET + float3(0.01);
            float3 boxMax = floor(pagePos) * PAGE_SIZE + PAGE_OFFSET + float3(PAGE_SIZE);

            float3 localStartPos = Common.CalculateRayAABBEntry(rayOrigin, rayDir, boxMin, boxMax);

            // Check if we are in a valid page
            uint pageIndex = Common.GetPageIndex(pagePos);

            // Check if the current voxel is within the page bounds
            if (Pages[pageIndex].voxelCount > 0)
            {
                // 使用计算出的进入点进行局部追踪
                if (LocalTraceRay(localStartPos, rayDir, boxMin, boxMax, outVertex, outNode))
                {
                    hit = true;
                    outHitDist = distance(outVertex.Position, rayOrigin);
                    break;
                }
            }

            // Step through the page
            if (pageTMax.x < pageTMax.y && pageTMax.x < pageTMax.z)
            {
                pagePos.x += stepSign.x;
                pageTMax.x += pagetDelta.x;
            }
            else if (pageTMax.y < pageTMax.z)
            {
                pagePos.y += stepSign.y;
                pageTMax.y += pagetDelta.y;
            }
            else
            {
                pagePos.z += stepSign.z;
                pageTMax.z += pagetDelta.z;
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
            0xff,
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
        return false;
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
            Common.traceInScreenSpace(offsetPos, rayDir, min(longDistanceInit, clampedTraceDistance), depthToleration, outVertex.Position, outVertex.Normal, outVertex.MaterialIndex))
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
