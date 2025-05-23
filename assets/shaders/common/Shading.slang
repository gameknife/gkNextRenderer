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
float3 EvaluateSH(float SHCoefficients[3][9], float3 normal, float rotate)
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

public float4 SampleIBL(uint skyIdx, float3 direction, float rotate, float roughness, in StructuredBuffer<SphericalHarmonics> HDRSHs, in Sampler2D[] TextureSamplers)
{
    if (roughness > 0.6f)
    {
        float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
        return float4(rayColor * 1.0f, 1.0);
    }
    float3 d = normalize(direction);
    float2 t = float2((atan2(d.x, d.z) + M_PI * rotate) * M_1_OVER_TWO_PI, acos(d.y) * M_1_PI);
    // Sample with explicit LOD based on roughness
    return min(float4(10, 10, 10, 1), TextureSamplers[NonUniformResourceIndex(skyIdx)].SampleLevel(t, roughness * 10.0f));
};

public float4 SampleIBLRough(uint skyIdx, float3 direction, float rotate, in StructuredBuffer<SphericalHarmonics> HDRSHs)
{
    float3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0 - rotate);
    return float4(rayColor * 1.0f, 1.0);
};

// 获取阴影值
public float getShadow(float3 worldPos, float3 jit, float3 normal, in UniformBufferObject ubo, in Sampler2D shadowmap)
{
    // 计算光源空间坐标
    float4 posInLightMap = mul(ubo.SunViewProjection, float4(worldPos + jit * 4.0f, 1.0f));

    // 将光源空间坐标转换到NDC空间 [-1,1] 再转到 [0,1] 的纹理空间
    float3 projCoords = posInLightMap.xyz / posInLightMap.w;
    projCoords = projCoords * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;

    float currentDepth = projCoords.z;

    float bias = 0.0005;

    float cosTheta = max(dot(normal, normalize(ubo.SunDirection.xyz)), 0.0);
    bias = lerp(0.0001, 0.00005, cosTheta);

    float closestDepth = shadowmap.Sample(projCoords.xy).x;
    float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;

    return shadow;
};

bool traceLongDistance(float3 position, float3 rayDir, float maxDistance, float depthTolerance, out float3 outPosition, out float3 outNormal, out uint outMaterialIdx,
                       in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices,
                       in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes)
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

public bool SoftwareRayTraceScene(float3 position, float3 rayDir, float3 normal, out float4 reflectionColor, in StructuredBuffer<Material> Materials,
                                  in ConstantBuffer<UniformBufferObject> Camera, in RWTexture2D<uint2> MiniGBuffer, in StructuredBuffer<NodeProxy> NodeProxies, in StructuredBuffer<float> Vertices,
                                  in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes)
{
    float3 hitPosition, hitNormal;
    uint hitMaterialIdx;
    reflectionColor = float4(0, 0, 0, 0);
    float3 offsetPos = position + normal * 0.01;
    if (traceLongDistance(offsetPos, rayDir, 5.0, 0.2, hitPosition, hitNormal, hitMaterialIdx, Camera, MiniGBuffer, NodeProxies, Vertices, Cubes, FarCubes))
    {
        Material hitMaterial = Materials[hitMaterialIdx];
        float4 hitAlbedo = hitMaterial.Diffuse;
        float4 hitAmbient = interpolateAmbientCubes<FullAmbientCubeSampler>(hitPosition, hitNormal, Cubes, FarCubes);
        reflectionColor = hitAlbedo * hitAmbient;
        return true;
    }
    return false;
};

public bool HardWareRayOcclusionCheck(
    RaytracingAccelerationStructure sceneBVH,
    float3 rayOrigin,
    float3 rayDir)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.TMin = 0.01f;
    ray.Direction = rayDir;
    ray.TMax = 1e4f;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
                   RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

    q.TraceRayInline(
        sceneBVH,
        rayFlags,
        0xff,
        ray);
    q.Proceed();

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        return true;
    }
    return false;
}

public bool HardWareRayTraceScene(float3 rayOrigin, float3 rayDir, float3 normal, out float4 reflectionColor, in StructuredBuffer<Material> Materials,
                                  in ConstantBuffer<UniformBufferObject> Camera, in StructuredBuffer<NodeProxy> NodeProxies,
                                  in StructuredBuffer<float> Vertices, in StructuredBuffer<uint> Indices, in StructuredBuffer<uint2> Offsets,
                                  in RaytracingAccelerationStructure sceneBVH, in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<AmbientCube> FarCubes)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.TMin = 0.01f;
    ray.Direction = rayDir;
    ray.TMax = 1e4f;
    RayQuery<RAY_FLAG_NONE> q;
    let rayFlags = RAY_FLAG_NONE;

    q.TraceRayInline(
        sceneBVH,
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

        const NodeProxy node = NodeProxies[q.CommittedRayInstanceId()];
        const uint2 offsets = Offsets[node.modelId];
        const uint indexOffset = offsets.x + q.CommittedRayPrimitiveIndex() * 3;
        const uint vertexOffset = offsets.y;
        const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset], Vertices);
        const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1], Vertices);
        const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2], Vertices);
        uint matIdx = FetchMaterialId(node, v0.MaterialIndex);
        const Material material = Materials[matIdx];
        const float3 normal = normalize(mul(WorldToObject, Mix(v0.Normal, v1.Normal, v2.Normal, BaryCoords)).xyz);

        // const vec2 texCoord = Mix(v0.TexCoord, v1.TexCoord, v2.TexCoord, PayloadData.BaryCoords);
        // const vec4 texColor = material.DiffuseTextureId >= 0 ? texture(TextureSamplers[nonuniformEXT(material.DiffuseTextureId)], texCoord) : vec4(1);
        // const vec3 albedo = material.MaterialModel == MaterialDiffuseLight ? vec3(0) : texColor.rgb * texColor.rgb * material.Diffuse.rgb;
        const float4 albedo = material.MaterialModel == MaterialDiffuseLight ? float4(0) : material.Diffuse;

        float3 hitPos = rayOrigin + rayDir * t;
        // 这里都是hit了，那就走间接光照，直接用hitpos和normal，从AmbientCube里取
        float4 indirectFromAmbientCube = interpolateAmbientCubes<FullAmbientCubeSampler>(hitPos, normal, Cubes, FarCubes);
        reflectionColor = albedo * indirectFromAmbientCube;
        return true;
    }
    reflectionColor = float4(0, 0, 0, 0);
    return false;
}
}

// emit primary ray, return hit-info
public interface IPrimaryRayCaster
{
    bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout float3 hitPos, inout float3 hitNormal, inout uint hitMaterialIdx);
}

// ray tracer for rendering
public interface IRayTracer
{
    bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout float t, inout float3 hitNormal, out Material hitMat, out uint hitMaterialIdx);
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
    void Render(IRayTracer tracer, IDirectIlluminator directIllum, float2 uv, uint2 vBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 albedo, inout float3 normal, inout float2 motion, inout float4 indirectColor);
}

public struct FHardwarePrimaryRayCaster : IPrimaryRayCaster
{
    public bool TracePrimaryRay(int2 ipos, int2 isize, inout uint4 RandomSeed, inout float3 hitPos, inout float3 hitNormal, inout uint hitMaterialIdx)
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
        bool hitted = HWTracer.TraceRay(origin.xyz, direction.xyz, hitNormal, t, hitNormal, hitMaterial, hitMaterialIdx);
        hitPos = origin.xyz + direction.xyz * t;
        return hitted;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    FHardwareRayTracer HWTracer;
}

public struct FHardwareRayTracer : IRayTracer
{
    public bool TraceOcclusion(float3 rayOrigin, float3 rayDir)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = 0.01f;
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
        ray.TMin = 0.01f;
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

    public bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout float t, inout float3 hitNormal, out Material hitMat, out uint hitMaterialIdx)
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.TMin = 0.01f;
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
            t = q.CommittedRayT();

            float2 TwoBaryCoords = q.CommittedRayBarycentrics();
            float3 BaryCoords = float3(1.0 - TwoBaryCoords.x - TwoBaryCoords.y, TwoBaryCoords.x, TwoBaryCoords.y);

            float4x3 WorldToObject = q.CommittedRayWorldToObject();

            const NodeProxy node = NodeProxies[q.CommittedRayInstanceId()];
            const uint2 offsets = Offsets[node.modelId];
            const uint indexOffset = offsets.x + q.CommittedRayPrimitiveIndex() * 3;
            const uint vertexOffset = offsets.y;
            const Vertex v0 = UnpackVertex(vertexOffset + Indices[indexOffset], Vertices);
            const Vertex v1 = UnpackVertex(vertexOffset + Indices[indexOffset + 1], Vertices);
            const Vertex v2 = UnpackVertex(vertexOffset + Indices[indexOffset + 2], Vertices);
            hitMaterialIdx = FetchMaterialId(node, v0.MaterialIndex);
            hitMat = Materials[hitMaterialIdx];
            hitNormal = normalize(mul(WorldToObject, Mix(v0.Normal, v1.Normal, v2.Normal, BaryCoords)).xyz);
            return true;
        }
        hitMat = Materials[0];
        hitMaterialIdx = 0;
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
    public bool TraceRay(float3 rayOrigin, float3 rayDir, float3 normal, inout float t, inout float3 hitNormal, out Material hitMat, out uint hitMaterialIdx)
    {
        float3 hitPosition;
        float3 offsetPos = rayOrigin + normal * 0.01;
        if (Common.traceLongDistance(offsetPos, rayDir, 5.0, 0.2, hitPosition, hitNormal, hitMaterialIdx, Camera, MiniGBuffer, NodeProxies, Vertices, Cubes, FarCubes))
        {
            hitMat = Materials[hitMaterialIdx];
            t = length(hitPosition - rayOrigin);
            return true;
        }
        hitMat = Materials[0];
        hitMaterialIdx = 0;
        return false;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    RWTexture2D<uint2> MiniGBuffer;

    StructuredBuffer<float> Vertices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<NodeProxy> NodeProxies;

    RWStructuredBuffer<AmbientCube> Cubes;
    RWStructuredBuffer<AmbientCube> FarCubes;
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
            if (Common.HardWareRayOcclusionCheck(Scene, position, lightVectorCone))
            {
                shadow = 0;
            }
        }
        directIllumColor = Camera.SunColor * d * shadow;
    }

    // all stuffs like pointers
    ConstantBuffer<UniformBufferObject> Camera;
    RaytracingAccelerationStructure Scene;
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
    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, float2 uv, uint2 vBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 albedo, inout float3 normal, inout float2 motion, inout float4 indirectColor)
    {

    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<float> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<uint2> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    StructuredBuffer<SphericalHarmonics> HDRSHs;
}

public struct FHybridRenderer : IRenderer
{
    public void Render(IRayTracer tracer, IDirectIlluminator directIllum, float2 uv, uint2 vBuffer, in Sampler2D[] TextureArray, inout uint4 RandomSeed, inout float4 albedo, inout float3 normal, inout float2 motion, inout float4 indirectColor)
    {
        // construct ray
        float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
        float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
        float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

        float3 ray_dir = normalize(dir.xyz);

        Vertex v = Common.get_material_data(vBuffer, origin.xyz, ray_dir, NodeProxies, Vertices);
        NodeProxy node = NodeProxies[vBuffer.x - 1];

        // geometry info
        normal = normalize(v.Normal.rgb);

        // motion info
        float4 currFrameHPos = mul(Camera.ViewProjection, float4(v.Position, 1));
        float2 currfpos = float2((currFrameHPos.xy / currFrameHPos.w * 0.5));
        float4 prevFrameHPos = mul(mul(Camera.PrevViewProjection, node.combinedPrevTS), float4(v.Position, 1));
        float2 prevfpos = float2((prevFrameHPos.xy / prevFrameHPos.w * 0.5));
        motion = prevfpos - currfpos;

        // mat info
        uint materialIndex = FetchMaterialId(node, v.MaterialIndex);
        Material mat = Materials[materialIndex];
        albedo = mat.Diffuse;
        float roughness = mat.Fuzziness;
        if (mat.DiffuseTextureId >= 0)
        {
            float4 tex = TextureArray[NonUniformResourceIndex(mat.DiffuseTextureId)].Sample(v.TexCoord);
            albedo *= tex;
        }
        if (mat.MRATextureId >= 0)
        {
            float4 mra = TextureArray[NonUniformResourceIndex(mat.MRATextureId)].Sample(v.TexCoord);
            roughness = roughness * mra.g;
        }

        // ibl
        const float dotValue = dot(ray_dir, normal);
        const float3 outwardNormal = dotValue > 0 ? -normal : normal;
        const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
        const float reflectProb = Schlick(cosine, mat.RefractionIndex);
        const float metalProb = mat.Metalness;

        indirectColor = float4(0, 0, 0, 0);

        const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples / 4);
        for (int i = 0; i < jitcount; i++)
        {

            bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
            bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
            bool chanceGGX = chanceReflect || chanceMetal;
            const float3 trace_next = chanceGGX ? reflect(ray_dir, outwardNormal) : outwardNormal;
            float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

            float4 reflectionColor;
            float t;
            float3 hitNormal;
            Material hitMat;
            uint hitMaterialIdx;
            if (tracer.TraceRay(v.Position, trace_dir, normal, t, hitNormal, hitMat, hitMaterialIdx))
            {
                float3 hitPos = v.Position + trace_dir * t;
                indirectColor += hitMat.Diffuse * interpolateAmbientCubes<FullAmbientCubeSampler>(hitPos, hitNormal, Cubes, FarCubes);
            }
            else
            {
                if (chanceGGX)
                {
                    indirectColor += Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, mat.Fuzziness, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
                }
                else
                {
                    indirectColor += interpolateAmbientCubes<SkyAmbientCubeSampler>(v.Position, normal, Cubes, FarCubes) * 2.0f;
                }
            }
        }
        indirectColor /= float(jitcount);

        float4 directColor = float4(0, 0, 0, 0);
        directIllum.DirectIlluminate(RandomSeed, v.Position, normal, directColor);
        indirectColor += directColor;
    }

    ConstantBuffer<UniformBufferObject> Camera;
    StructuredBuffer<float> Vertices;
    StructuredBuffer<uint> Indices;
    StructuredBuffer<Material> Materials;
    StructuredBuffer<uint2> Offsets;
    StructuredBuffer<NodeProxy> NodeProxies;

    RWStructuredBuffer<AmbientCube> Cubes;
    RWStructuredBuffer<AmbientCube> FarCubes;
    StructuredBuffer<SphericalHarmonics> HDRSHs;
}
