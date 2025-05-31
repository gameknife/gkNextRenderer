implementing Common;
#include "PreProcessor.slang"

static const float2 haltonSeq[8] = {
    float2(0.5, 0.3333),
    float2(0.25, 0.6667),
    float2(0.75, 0.1111),
    float2(0.125, 0.4444),
    float2(0.625, 0.7778),
    float2(0.375, 0.2222),
    float2(0.875, 0.5556),
    float2(0.0625, 0.8889)
};

namespace Common
{
    public float2 CalculateMotionVector(ConstantBuffer<UniformBufferObject> Camera, NodeProxy node, Vertex v )
    {
        // motion info
        float4 currFrameHPos = mul(Camera.ViewProjection, float4(v.Position, 1));
        float2 currfpos = currFrameHPos.xy / currFrameHPos.w * 0.5;
        float4 prevFrameHPos = mul(mul(Camera.PrevViewProjection, node.combinedPrevTS), float4(v.Position, 1));
        float2 prevfpos = prevFrameHPos.xy / prevFrameHPos.w * 0.5;
        return prevfpos - currfpos;
    }

    public void FetchGBuffer(in Vertex inVertex, in Material inMaterial, in Sampler2D[] TextureArray, out float4 outAlbedo, out float4 outNormal)
    {
        outAlbedo = inMaterial.Diffuse;

        if (inMaterial.DiffuseTextureId >= 0)
        {
            float4 tex = TextureArray[NonUniformResourceIndex(inMaterial.DiffuseTextureId)].Sample(inVertex.TexCoord);
            outAlbedo *= tex;
        }
        float roughness = inMaterial.Fuzziness;
        if (inMaterial.MRATextureId >= 0)
        {
            float4 mra = TextureArray[NonUniformResourceIndex(inMaterial.MRATextureId)].Sample(inVertex.TexCoord);
            roughness = roughness * mra.g;
        }

        outNormal = float4(inVertex.Normal.rgb, roughness);
    }

    public float3 EvaluateSH(float SHCoefficients[3][9], float3 normal, float rotate)
    {
        // Apply rotation around Y-axis (0 to 2 maps to 0 to 360 degrees)
        float angle = rotate * 3.14159265358979323846f;
        float cosAngle = cos(angle);
        float sinAngle = sin(angle);

        // Rotate the normal vector around Y-axis
        float3 rotatedNormal = float3(
            normal.x * cosAngle + normal.z * sinAngle,
            normal.y,
            -normal.x * sinAngle + normal.z * cosAngle);

        // SH basis function evaluation
        static const float SH_C0 = 0.282095f;
        static const float SH_C1 = 0.488603f;
        static const float SH_C2 = 1.092548f;
        static const float SH_C3 = 0.315392f;
        static const float SH_C4 = 0.546274f;

        float basis[9];
        basis[0] = SH_C0;
        basis[1] = -SH_C1 * rotatedNormal.y;
        basis[2] = SH_C1 * rotatedNormal.z;
        basis[3] = -SH_C1 * rotatedNormal.x;
        basis[4] = SH_C2 * rotatedNormal.x * rotatedNormal.y;
        basis[5] = -SH_C2 * rotatedNormal.y * rotatedNormal.z;
        basis[6] = SH_C3 * (3.f * rotatedNormal.y * rotatedNormal.y - 1.0f);
        basis[7] = -SH_C2 * rotatedNormal.x * rotatedNormal.z;
        basis[8] = SH_C4 * (rotatedNormal.x * rotatedNormal.x - rotatedNormal.z * rotatedNormal.z);

        float3 color = float3(0.0, 0.0, 0.0);
        for (int i = 0; i < 9; ++i)
        {
            color.r += SHCoefficients[0][i] * basis[i];
            color.g += SHCoefficients[1][i] * basis[i];
            color.b += SHCoefficients[2][i] * basis[i];
        }

        return color;
    };

    public float4 SampleIBLRough(uint skyIdx, float3 direction, float rotate, in StructuredBuffer<SphericalHarmonics> HDRSHs)
    {
        float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
        return float4(rayColor * 1.0f, 1.0);
    };

    public float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness, in StructuredBuffer<SphericalHarmonics> HDRSHs, in Sampler2D[] TextureSamplers)
    {
        if (roughness > 0.6f)
        {
            return SampleIBLRough(skyIdx, direction, rotate, HDRSHs);
        }
        float3 d = normalize(direction);
        float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
        return min(float4(10, 10, 10, 1), TextureSamplers[NonUniformResourceIndex(skyIdx)].SampleLevel(t, roughness * 10.0f));
    };

    public float getShadow(float3 worldPos, float3 jit, float3 normal, in UniformBufferObject ubo, in Sampler2D shadowmap)
    {
        float4 posInLightMap = mul(ubo.SunViewProjection, float4(worldPos + jit * 4.0f, 1.0f));

        float3 projCoords = posInLightMap.xyz / posInLightMap.w;
        projCoords = projCoords * 0.5 + 0.5;
        projCoords.y = 1.0 - projCoords.y;

        float currentDepth = projCoords.z;
        float cosTheta = max(dot(normal, normalize(ubo.SunDirection.xyz)), 0.0);
        float bias = lerp(0.0001, 0.00005, cosTheta);

        float closestDepth = shadowmap.Sample(projCoords.xy).x;
        float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;

        return shadow;
    };

    bool traceLongDistance(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx,
                        in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices,
                        in StructuredBuffer<AmbientCube> Cubes, in StructuredBuffer<AmbientCube> FarCubes)
    {
        // Near field tracing parameters
        const float nearDistance = 0.25;            // Near field tracing distance
        const float longDistanceInit = 0.5;         // Far field starting distance (to avoid self-intersection)
        const float stepSize = 0.25;                // Far field step size
        const float maxTotalDistance = maxDistance; // Maximum total tracing distance

        if (traceInScreenSpace(position, rayDir, nearDistance, depthTolerance, outPosition, outNormal, outMaterialIdx, Camera, MiniGBuffer, NodeProxies, Vertices))
        {
            return true;
        }

        float tracedDistance = longDistanceInit;
        float3 currentPos = position + rayDir * longDistanceInit;

        while (tracedDistance < maxTotalDistance)
        {
            float3 probePos = (currentPos - CUBE_OFFSET) / CUBE_UNIT;
            uint matId = 0;
            bool isInSolid = inSolid(probePos, matId, Cubes, FarCubes);

            if (isInSolid)
            {
                outPosition = currentPos;
                outNormal = float3(0, 1, 0);
                outMaterialIdx = matId;
                return true;
            }
            currentPos += rayDir * stepSize;
            tracedDistance += stepSize;
        }
        return false;
    };

    bool traceInScreenSpace(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx,
                            in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices)
    {
        const int maxSteps = 5;
        float stepSize = maxDistance / float(maxSteps);
        float3 rayStart = position + rayDir * 0.1;

        outPosition = float3(0, 0, 0);
        outNormal = float3(0, 0, 1);
        outMaterialIdx = 0;

        for (int i = 0; i < maxSteps; i++)
        {
            float3 currentPos = rayStart + rayDir * (i * stepSize);
            float4 currentPosProj = mul(Camera.ViewProjection, float4(currentPos, 1.0));
            currentPosProj.xyz /= currentPosProj.w;
            float2 currentUV = currentPosProj.xy * 0.5 + 0.5;
            if (any(currentUV < float2(0.0)) || any(currentUV > float2(1.0)))
                continue;

            int2 size;
            MiniGBuffer.GetDimensions(size.x, size.y);

            int2 sampleCoord = int2(currentUV * float2(size));
            uint2 vBufferSample = MiniGBuffer.Load(sampleCoord).rg;
            if (vBufferSample.r == 0)
                continue;

            float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float4 target = mul(Camera.ProjectionInverse, float4(currentPosProj.x, currentPosProj.y, 1, 1));
            float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
            float3 ray_dir = normalize(dir.xyz);

            Vertex sampleVertex = Common.get_material_data(vBufferSample, origin.xyz, ray_dir, NodeProxies, Vertices);

            float viewSpaceCurrentDepth = mul(Camera.ModelView, float4(currentPos, 1.0)).z;
            float viewSpaceSampleDepth = mul(Camera.ModelView, float4(sampleVertex.Position, 1.0)).z;

            if (viewSpaceCurrentDepth < viewSpaceSampleDepth && viewSpaceCurrentDepth > viewSpaceSampleDepth - depthTolerance)
            {
                outPosition = sampleVertex.Position;
                outNormal = normalize(sampleVertex.Normal.rgb);
                NodeProxy hitNode = NodeProxies[vBufferSample.x - 1];
                outMaterialIdx = hitNode.matId[sampleVertex.MaterialIndex];
                return true;
            }
        }
        return false;
    };
}

// emit primary ray, return hit-info
public interface IPrimaryRayCaster
{
    bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode);
}

// ray tracer for rendering
public interface IRayTracer
{
    bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout Vertex outVertex, inout NodeProxy outNode);
    bool TraceOcclusion(float3 rayOrigin, float3 rayDir);
    bool TraceSegment(float3 rayOrigin, float3 rayTarget);
}

// direct illum 
public interface IDirectIlluminator
{
    void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor);
}

// renderer
public interface IRenderer
{
    void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor);
}

public struct FHardwarePrimaryRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode)
    {
        float2 uvOffset = float2(0);
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        if (Camera.TAA)
        {
            // here should offset a non-random offset and consider in motion vector
            uvOffset = (haltonSeq[Camera.TotalFrames % 8] - float2(0.5, 0.5)) / isize;
        }
        uv += uvOffset;
        float2 offset = Camera.Aperture / 2 * RandomInUnitDisk(RandomSeed);
        float4 origin = mul(Camera.ModelViewInverse, float4(offset, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 direction = mul(Camera.ModelViewInverse, float4(normalize(target.xyz * Camera.FocusDistance - float3(offset, 0)), 0));

        Material hitMaterial;
        float t = 0;
        bool hitted = HWTracer.TraceRay(origin.xyz, direction.xyz, outVertex.Normal, outVertex, outNode);
        //outVertex.Position = origin.xyz + direction.xyz * t;
        return hitted;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    FHardwareRayTracer HWTracer;
}

public struct FVisibilityBufferRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode)
    {
        uint2 vBuffer = VisibilityBuffer[ipos].rg;
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // construct ray
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

        float3 ray_dir = normalize(dir.xyz);

        Vertex v = Common.get_material_data(vBuffer, origin.xyz, ray_dir, NodeProxies, Vertices);
        outNode = NodeProxies[vBuffer.x - 1];

        // geometry info
        outVertex.Position = v.Position;
        outVertex.Normal = normalize(v.Normal.rgb);
        outVertex.MaterialIndex = FetchMaterialId(outNode, v.MaterialIndex);
        outVertex.TexCoord = v.TexCoord;
        outVertex.Tangent = v.Tangent;

        if (vBuffer.x == 0)
        {
            return false;
        }

        return true;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<float> Vertices;
    StructuredBuffer<NodeProxy> NodeProxies;
    RWTexture2D<uint2> VisibilityBuffer;
}

public struct FVoxelRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout Vertex outVertex, inout NodeProxy outNode)
    {
        float2 uv = (float2(ipos) / float2(isize)) * 2.0 - 1.0;
        
        // construct ray
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

        float3 ray_dir = normalize(dir.xyz);

        return DDARayTracer.TraceRay(origin.xyz, ray_dir, outVertex.Normal, outVertex, outNode);
    }

    ConstantBuffer<UniformBufferObject> Camera;
    FVoxelDDARayTracer DDARayTracer;
}

public struct FVoxelDDARayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        return false;
    }
    public bool TraceSegment(float3 rayOrigin, float3 rayTaget)
    {
        return false;
    }
    public bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout Vertex outVertex, inout NodeProxy outNode)
    {
        uint hitMatId = 0;

        uint maxSteps = 500; // 0.25f per step, so 500 steps is about 125m

        // DDA voxel traversal
        float3 voxelPos = (rayOrigin.xyz - CUBE_OFFSET) / CUBE_UNIT;
        float3 tDelta = abs(CUBE_UNIT / rayDir); // Step size along each axis
        float3 tMax = float3(0, 0, 0);           // Distance to next voxel boundary
        float3 stepSign = sign(rayDir);          // Direction to step

        // Calculate initial tMax values
        if (rayDir.x != 0) tMax.x = (stepSign.x > 0 ? ceil(voxelPos.x) - voxelPos.x : voxelPos.x - floor(voxelPos.x)) * tDelta.x;
        if (rayDir.y != 0) tMax.y = (stepSign.y > 0 ? ceil(voxelPos.y) - voxelPos.y : voxelPos.y - floor(voxelPos.y)) * tDelta.y;
        if (rayDir.z != 0) tMax.z = (stepSign.z > 0 ? ceil(voxelPos.z) - voxelPos.z : voxelPos.z - floor(voxelPos.z)) * tDelta.z;

        float3 lastPos = voxelPos;
        bool hit = false;
        float3 normal = float3(0, 0, 0);
        int stepAxis = 0; // 0=x, 1=y, 2=z

        // Traverse the grid
        for (uint i = 0; i < maxSteps; i++)
        {
            // Check current voxel
            if (inSolid(voxelPos, hitMatId, Cubes, FarCubes))
            {
                hit = true;

                outVertex.Normal = normalize(normal);
                outVertex.Position = CUBE_OFFSET + (lastPos + normal) * CUBE_UNIT;
                outVertex.MaterialIndex = hitMatId;
                outVertex.TexCoord = float2(0.5, 0.5);
                break;
            }

            // Store last position before stepping
            lastPos = voxelPos;

            // Reset normal
            normal = float3(0, 0, 0);

            // Find the closest axis to step along
            if (tMax.x < tMax.y && tMax.x < tMax.z)
            {
                voxelPos.x += stepSign.x;
                tMax.x += tDelta.x;
                normal.x = -stepSign.x; // Set normal based on the face we're crossing
                stepAxis = 0;
            }
            else if (tMax.y < tMax.z)
            {
                voxelPos.y += stepSign.y;
                tMax.y += tDelta.y;
                normal.y = -stepSign.y; // Set normal based on the face we're crossing
                stepAxis = 1;
            }
            else
            {
                voxelPos.z += stepSign.z;
                tMax.z += tDelta.z;
                normal.z = -stepSign.z; // Set normal based on the face we're crossing
                stepAxis = 2;
            }
        }
        return hit;
    }

    StructuredBuffer<AmbientCube> Cubes;
    StructuredBuffer<AmbientCube> FarCubes;
}

public struct FHardwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = 1e4f;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Scene,
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceSegment(float3 rayOrigin, float3 rayTarget)
    {
        float3 dir = rayTarget - rayOrigin;
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.TMax = length(dir);
        ray.Direction = dir / ray.TMax;
        
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
        let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                       RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

        q.TraceRayInline(
            Scene,
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    }

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout Vertex outVertex, inout NodeProxy outNode)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = EPS;
        ray.Direction = rayDir;
        ray.TMax = 1e4f;
        RayQuery<RAY_FLAG_NONE> q;
        let rayFlags = RAY_FLAG_NONE;

        q.TraceRayInline(
            Scene,
            rayFlags,
            0xff,
            ray);

        q.Proceed();
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float t = q.CommittedRayT();

            float2 TwoBaryCoords = q.CommittedRayBarycentrics();
            float3 BaryCoords = float3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);

            float4x3 WorldToObject = q.CommittedRayWorldToObject();

            outNode = NodeProxies[q.CommittedRayInstanceId()];
            const uint2 offsets = Offsets[outNode.modelId];
            const uint indexOffset = offsets.x + q.CommittedPrimitiveIndex() * 3;
            const uint vertexOffset = offsets.y;
            const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset], Vertices);
            const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1], Vertices);
            const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2], Vertices);
            outVertex.MaterialIndex = FetchMaterialId(outNode, v0.MaterialIndex);
            outVertex.Normal = normalize(mul(WorldToObject, Mix(v0.Normal, v1.Normal, v2.Normal, BaryCoords)).xyz);
            outVertex.TexCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, BaryCoords);
            outVertex.Position = rayOrigin + rayDir * t;// * (t - EPS2);
            //outVertex.Tangent = normalize(mul(WorldToObject, Mix(v0.Tangent, v1.Tangent, v2.Tangent, BaryCoords)).xyz);
            return true;
        }
        q.CandidatePrimitiveIndex();
        return false;
    }

    StructuredBuffer<float> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<uint2> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;
    RaytracingAccelerationStructure Scene;
}

public struct FSoftwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        return false;
    }
    public bool TraceSegment(float3 rayOrigin, float3 rayTaget)
    {
        return false;
    }
    public bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout Vertex outVertex, inout NodeProxy outNode)
    {
        float3 offsetPos = rayOrigin + normal * 0.01;
        if (Common.traceLongDistance(offsetPos, rayDir, 5.0, 0.2, outVertex.Position, outVertex.Normal, outVertex.MaterialIndex, Camera, MiniGBuffer, NodeProxies, Vertices, Cubes, FarCubes))
        {
            return true;
        }
        return false;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    RWTexture2D<uint2> MiniGBuffer;

    StructuredBuffer<float> Vertices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<NodeProxy> NodeProxies;

    StructuredBuffer<AmbientCube> Cubes;
    StructuredBuffer<AmbientCube> FarCubes;
}

public struct FHardwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {

        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            const float3 lightVector = Camera.SunDirection.xyz;
            const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
            shadow = 1;
            if (HWTracer.TraceOcclusion(position, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    // all stuffs like pointers
    ConstantBuffer<UniformBufferObject> Camera;
    FHardwareRayTracer HWTracer;
}

public struct FSoftwareDirectIlluminator : IDirectIlluminator
{
    public void DirectIlluminate(inout uint4 RandomSeed, float3 position, float3 normal, inout float4 directIllumColor)
    {
        float shadow = 0.0f;
        const float3 lightVector = Camera.SunDirection.xyz;
        const float4 d = max(dot(lightVector, normalize(normal)), 0.0) * M_1_PI;
        if (Camera.HasSun)
        {
            float3 jitter = (RandomFloat3(RandomSeed) - float3(0.5f)) * 0.125f;
            shadow += Common.getShadow(position, jitter, normal, Camera, ShadowMapSampler);
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    Sampler2D ShadowMapSampler;
}

public struct FPathTracingRenderer : IRenderer
{
    bool GetRayColor(IRayTracer tracer, inout Vertex inVertex, inout float3 rayDir, inout float4 rayColor, inout uint4 RandomSeed, in Sampler2D[] TextureArray)
    {
        Material mat = Materials[inVertex.MaterialIndex];
        float roughness = mat.Fuzziness;
        const float dotValue = dot(rayDir, inVertex.Normal);
        const bool BackFace = dotValue > 0;
        const float3 outwardNormal = BackFace ? -inVertex.Normal : inVertex.Normal;
        const float niOverNt = BackFace ? mat.RefractionIndex2 : (1 / mat.RefractionIndex2);
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);
        const float metalProb = mat.Metalness;

        bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
        bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
        bool chanceGGX = chanceReflect || chanceMetal;
        const float3 trace_next = chanceGGX ? reflect(rayDir, outwardNormal) : outwardNormal;
        float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);
        

        if (mat.MaterialModel == MaterialDielectric)
        {
            if (!chanceReflect)
            {
                trace_dir = refract(rayDir, outwardNormal, niOverNt);
            }
        }

        rayDir = trace_dir;

        NodeProxy hitNode;
        if (tracer.TraceRay(inVertex.Position, trace_dir, inVertex.Normal, inVertex, hitNode))
        {
            Material hitMat = Materials[inVertex.MaterialIndex];
            float4 outAlbedo = hitMat.Diffuse;
            if (hitMat.DiffuseTextureId >= 0)
            {
                float4 tex = TextureArray[NonUniformResourceIndex(hitMat.DiffuseTextureId)].Sample(inVertex.TexCoord);
                outAlbedo *= tex;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight || !chanceReflect)
            {
                rayColor *= outAlbedo;
            }
            if (BackFace && mat.MaterialModel != MaterialDielectric)
            {
                rayColor = 0.0f;
                return true;
            }
            if (hitMat.MaterialModel == MaterialDiffuseLight)
            {
                return true;
            }
            return false;
        }
        else
        {
            float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
            // miss, FinalColor to SkyIBL
            rayColor *= skyColor;
            return true;
        }
    }

    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor)
    {
        FinalColor = float4(0, 0, 0, 0);
        Material mat = Materials[inVertex.MaterialIndex];
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalColor = mat.Diffuse;
            return;
        }

        const uint sample = Camera.FastGather ? 1 : Camera.NumberOfSamples;
        for (int i = 0; i < sample; i++)
        {
            float4 RayColor = float4(1, 1, 1, 1);

            float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
            float3 ray_dir = normalize(inVertex.Position - eye.xyz);

            float3 direction = ray_dir;
            Vertex vertexStart = inVertex;

            uint maxBounces = mat.MaterialModel == MaterialDielectric ? Camera.MaxNumberOfBounces : Camera.NumberOfBounces;

            // Path tracing 本质上是一个递归的过程，所以这里要再抽象一个private函数
            for (uint b = 0; b < maxBounces; ++b)
            {
                if ( GetRayColor(tracer, vertexStart, direction, RayColor, RandomSeed, TextureArray) )
                {
                    break;
                }

                // 提前退出
                const bool earlyExit = b > 0 && (mat.MaterialModel != MaterialDielectric) && (RandomFloat(RandomSeed) < 0.5f);

                // 终结路径
                if (b == maxBounces - 1 || earlyExit)
                {
                    RayColor *= interpolateAmbientCubes<LightCacheAmbientCubeSampler>(vertexStart.Position, vertexStart.Normal, Cubes, FarCubes);// * 2.0f;
                    break;
                }
            }

            FinalColor += RayColor;
        }

        FinalColor /= float(sample);

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed, inVertex.Position, inVertex.Normal, directColor);
        FinalColor += directColor;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<float> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<uint2> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    StructuredBuffer<AmbientCube> Cubes;
    StructuredBuffer<AmbientCube> FarCubes;
    StructuredBuffer<SphericalHarmonics> HDRSHs;
}

public struct FHybridRenderer : IRenderer
{
    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, Vertex inVertex, float4 inGBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 FinalColor)
    {
        // construct ray
        float4 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float3 ray_dir = normalize(inVertex.Position - eye.xyz);

        Material mat = Materials[inVertex.MaterialIndex];
        if (mat.MaterialModel == MaterialDiffuseLight)
        {
            FinalColor = mat.Diffuse;
            return;
        }
        float roughness = inGBuffer.a;

        // ibl
        const float dotValue = dot(ray_dir, inVertex.Normal);
        const float3 outwardNormal = dotValue > 0 ? -inVertex.Normal : inVertex.Normal;
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);
        const float metalProb = mat.Metalness;

        FinalColor = float4(0, 0, 0, 0);

        //const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples / 4);
        const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples);
        for (int i = 0; i < jitcount; i++)
        {
            bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
            bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
            bool chanceGGX = chanceReflect || chanceMetal;
            const float3 trace_next = chanceGGX ? reflect(ray_dir, outwardNormal) : outwardNormal;
            float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

            float4 reflectionColor;
            Vertex hitVertex;
            NodeProxy hitNode;
            if (tracer.TraceRay(inVertex.Position, trace_dir, inVertex.Normal, hitVertex, hitNode))
            {
                Material hitMat = Materials[hitVertex.MaterialIndex];
                float4 outAlbedo = hitMat.Diffuse;
                if (hitMat.MaterialModel == MaterialDiffuseLight)
                {
                    FinalColor += outAlbedo;
                }
                else
                {
                    if (chanceReflect && !chanceMetal)
                    {
                        outAlbedo = float4(1, 1, 1, 1);
                    }
                    FinalColor += outAlbedo * interpolateAmbientCubes<LightCacheAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, FarCubes);
                }
            }
            else
            {
                FinalColor += Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
                // if (chanceGGX)
                // {
                //     FinalColor += Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
                // }
                // else
                // {
                //     FinalColor += interpolateAmbientCubes<SkyAmbientCubeSampler>(inVertex.Position, inVertex.Normal, Cubes, FarCubes) * 2.0f;
                // }
            }
        }
        FinalColor /= float(jitcount);

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed, inVertex.Position, inVertex.Normal, directColor);
        FinalColor += directColor;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<float> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<uint2> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    StructuredBuffer<AmbientCube> Cubes;
    StructuredBuffer<AmbientCube> FarCubes;
    StructuredBuffer<SphericalHarmonics> HDRSHs;
}
