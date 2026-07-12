#include "PreProcessor.slang"

// Probe baker for ambient cubes.
// Split out of AmbientCube.slang: AmbientCube.slang owns sampler interfaces and
// runtime fetch helpers; this file owns the bake-side logic that writes back into
// the cube/voxel buffers (FaceTask tracing, distance/inside detection). Tracing
// always bakes direct+indirect: the indirect bounce samples neighbour cubes via
// interpolateAmbientCubesStable, so there is no separate propagation pass.

uint AmbientBakeHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float AmbientBakeHash01(uint value)
{
    return float(AmbientBakeHash(value) & 0x00FFFFFFu) / 16777216.0f;
}

float AmbientBakeRadicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 AmbientBakeHammersley2D(uint index, uint sampleCount)
{
    return float2((float(index) + 0.5f) / float(sampleCount), AmbientBakeRadicalInverseVdc(index));
}

float3 AmbientBakeUniformHemisphere(float2 u)
{
    float z = saturate(u.x);
    float phi = M_TWO_PI * u.y;
    float r = sqrt(max(0.0f, 1.0f - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

uint AmbientBakeFaceId(float3 basis)
{
    if (abs(basis.y) > 0.5f)
    {
        return basis.y > 0.0f ? 0u : 1u;
    }
    if (abs(basis.x) > 0.5f)
    {
        return basis.x > 0.0f ? 2u : 3u;
    }
    return basis.z > 0.0f ? 4u : 5u;
}

float2 AmbientBakeSample2D(uint sampleIndex, uint sampleCount, uint scramble)
{
    float2 u = AmbientBakeHammersley2D(sampleIndex, sampleCount);
    float2 shift = float2(AmbientBakeHash01(scramble), AmbientBakeHash01(scramble ^ 0x9e3779b9u));
    return frac(u + shift);
}

float AmbientCubeAccumulationWeight(uint sampleCount)
{
    return 1.0f / float(min(sampleCount, 254u) + 1u);
}

void AdvanceAmbientCubeSampleCount(inout uint4 skyvis_pxnxs0s1, uint sampleCount)
{
    skyvis_pxnxs0s1.z = min(sampleCount + 1u, 255u);
}

public struct FGpuProbeGenerator : IProbeGenerator
{
    bool FaceTask(IRayTracer tracer, float3 origin, float3 basis, uint iterate, uint bakeCascadeIndex,
                  inout uint IndirectLight, out float4 outSunDirect, out float4 outSkyEmissiveDirect,
                  out float outSkyVisibility, float currWeight)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float cubeUnit = GetAmbientCubeUnit();

        origin = origin + basis * cubeUnit * 0.25f;
        // 输出结果
        float4 sunDirect = float4(0.0);
        float4 skyEmissiveDirect = float4(0.0);
        float4 bounceColor = float4(0.0);

        float skyVisibility = 0.0;

        uint scramble = AmbientBakeHash(asuint(origin.x) ^ (asuint(origin.y) * 0x9e3779b9u) ^
                                        (asuint(origin.z) * 0x85ebca6bu));
        scramble ^= AmbientBakeHash(AmbientBakeFaceId(basis) + bakeCascadeIndex * 17u);
        scramble ^= AmbientBakeHash(iterate * 131u + Camera.TotalFrames * 8191u);

        // 这个采样还是有点过重，可以考虑用probe间的光能传递来完成，而非直接tracing
        for (uint i = 0; i < FACE_TRACING; i++)
        {
            float2 sampleUv = AmbientBakeSample2D(i, FACE_TRACING, scramble);
            float3 hemiVec = AmbientBakeUniformHemisphere(sampleUv);
            float3 rayDir = AlignWithNormal(hemiVec, basis);

            Vertex hitVertex;
            NodeProxy hitNode;
            float4 tempColor = float4(0.0);
            float hitDist = 0;
            if (tracer.TraceRay(origin, rayDir, FAST_MAX_TRACE_DISTANCE, hitVertex, hitNode, tempColor, hitDist))
            {
                Material hitMaterial = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
                float4 outAlbedo = hitMaterial.Diffuse;
                if (hitMaterial.DiffuseTextureId >= 0)
                {
                    float4 tex = Bindless.GetSampleTexture(hitMaterial.DiffuseTextureId).SampleLevel(hitVertex.TexCoord, 0);
                    outAlbedo *= tex;
                }

                if (hitMaterial.MaterialModel == MaterialDiffuseLight)
                {
                    skyEmissiveDirect += outAlbedo;
                }
                else
                {
                    MarkAmbientBrickHitAtWorld(hitVertex.Position, true);
                    bounceColor += outAlbedo * interpolateAmbientCubesStable<DIAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, bakeCascadeIndex) * 2.0f; // magic bounce twice
                }
            }
            else
            {
                skyEmissiveDirect += Common.SampleIBLRough(Camera.SkyIdx, rayDir, Camera.SkyRotation) * (Camera.HasSky ? Camera.SkyIntensity : 0.0f);
                skyVisibility += 1.0f;
            }
        }

        skyEmissiveDirect = skyEmissiveDirect / float(FACE_TRACING);
        bounceColor = bounceColor / float(FACE_TRACING);
        skyVisibility = skyVisibility / float(FACE_TRACING);

        // // 太阳光
        if (Camera.HasSun)
        {
            float3 sunDir = Camera.SunDirection.xyz;
            float sunAtten = tracer.TraceOcclusion(origin, sunDir) ? 0.0f : 1.0f;
            float ndotl = clamp(dot(basis, sunDir), 0.0f, 1.0f);
            sunDirect += Camera.SunColor * sunAtten * ndotl * (Camera.HasSun ? 1.0f : 0.0f) * 0.25f;
        }

        IndirectLight = LerpPackedColorAlt(IndirectLight, bounceColor, currWeight);
        outSunDirect = sunDirect;
        outSkyEmissiveDirect = skyEmissiveDirect;
        outSkyVisibility = skyVisibility;
        return false;
    }

    float DetectDistance(IRayTracer tracer, float3 origin, float3 rayDir)
    {
        float cubeUnit = GetAmbientCubeUnit();
        Vertex hitVertex;
        NodeProxy hitNode;
        float4 tempColor = float4(0.0);
        uint TempInstanceId;
        float hitDist = 0;
        if (tracer.TraceRay(origin, rayDir, cubeUnit * 64, hitVertex, hitNode, tempColor, hitDist))
        {
            return hitDist;
        }
        return 255;
    }

    bool InsideGeometry(IRayTracer tracer, inout float3 origin, float3 rayDir, inout uint OutMaterialId, inout float OutDistanceToSolid)
    {
        float cubeUnit = GetAmbientCubeUnit();
        // 求交测试
        Vertex hitVertex;
        NodeProxy hitNode;
        float4 tempColor;
        float hitDist = 0;
        if (tracer.TraceRay(origin, rayDir, cubeUnit * 64, hitVertex, hitNode, tempColor, hitDist))
        {
            OutDistanceToSolid = hitDist;

            if (OutDistanceToSolid <= cubeUnit)
            {
                Material hitMaterial = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
                OutMaterialId = hitVertex.MaterialIndex;

                if (dot(hitVertex.Normal, rayDir) > 0.0 || ((hitMaterial.MaterialModel == MaterialDiffuseLight)))// && hitDist < 0.02f)) //voxel inclusive
                {
                    OutDistanceToSolid = 0;
                    return true;
                }
            }
        }
        return false;
    }

    // cubeIdx is the cube-pool element index for this probe (-1 when its brick is unallocated, in
    // which case only the dense voxel fields at voxelIdx are written). voxelIdx is the dense voxel index.
    public void Render(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
        float cubeUnit = GetAmbientCubeUnit();



        float distPY = 255.0f;
        float distNY = 255.0f;
        float distPX = 255.0f;
        float distNX = 255.0f;
        float distPZ = 255.0f;
        float distNZ = 255.0f;

        // this will be filled by cpu, may trigger crash on amd gpu,
        // Voxels[voxelIdx].matId = 0;
        // InsideGeometry(tracer, origin, float3(0, 1, 0), Voxels[voxelIdx].matId, distPY);
        // InsideGeometry(tracer, origin, float3(0, -1, 0), Voxels[voxelIdx].matId, distNY);
        // InsideGeometry(tracer, origin, float3(1, 0, 0), Voxels[voxelIdx].matId, distPX);
        // InsideGeometry(tracer, origin, float3(-1, 0, 0), Voxels[voxelIdx].matId, distNX);
        // InsideGeometry(tracer, origin, float3(0, 0, 1), Voxels[voxelIdx].matId, distPZ);
        // InsideGeometry(tracer, origin, float3(0, 0, -1), Voxels[voxelIdx].matId, distNZ);

        float minDist = min(min(min(distPY, distNY), min(distPX, distNX)), min(distPZ, distNZ));
        if (minDist > 254.0f)
        {
            minDist = min( minDist, DetectDistance(tracer, origin, float3(1, 1, 1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(-1, 1, 1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(-1, -1, 1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(-1, 1, 1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(1, 1, -1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(-1, 1, -1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(-1, -1, -1)) );
            minDist = min( minDist, DetectDistance(tracer, origin, float3(-1, 1, -1)) );
        }

        distPY = saturate(distPY * 4.0);
        distNY = saturate(distNY * 4.0);
        distPX = saturate(distPX * 4.0);
        distNX = saturate(distNX * 4.0);
        distPZ = saturate(distPZ * 4.0);
        distNZ = saturate(distNZ * 4.0);

        float inside = distPY * distNY * distPX * distNX * distPZ * distNZ;

        Voxels[voxelIdx].distanceToSolid = PackNibbles(
            uint4(min(uint(minDist / cubeUnit), 15u), uint(inside * 15.0f), uint(distPZ * 15.0f), uint(distNZ * 15.0f)),
            uint4(uint(distPX * 15.0f), uint(distNX * 15.0f), uint(distPY * 15.0f), uint(distNY * 15.0f)));

        if (minDist < 8 && cubeIdx >= 0) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            uint iterate = Bindless.GetGpuscene().Camera[0].TotalFrames;

            uint bakeCascadeIndex = Bindless.GetGpuscene().CustomData1;
            uint4 skyvis_pznzpyny = UnpackBytes(Cubes[cubeIdx].SkyVisibilityPZNZPYNY);
            uint4 skyvis_pxnxs0s1 = UnpackBytes(Cubes[cubeIdx].SkyVisibilityPXNXS0S1);
            uint sampleCount = skyvis_pxnxs0s1.z;
            float currWeight = AmbientCubeAccumulationWeight(sampleCount);
            float4 faceSun;
            float4 faceSkyEmissive;
            float faceSkyVis;
            float4 sunDirect = float4(0.0f);
            float4 skyEmissiveDirect = float4(0.0f);

            FaceTask(tracer, origin, float3(0, 1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosY, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.z = uint(lerp(float(skyvis_pznzpyny.z), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegY, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.w = uint(lerp(float(skyvis_pznzpyny.w), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosX, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pxnxs0s1.x = uint(lerp(float(skyvis_pxnxs0s1.x), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegX, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pxnxs0s1.y = uint(lerp(float(skyvis_pxnxs0s1.y), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosZ, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.x = uint(lerp(float(skyvis_pznzpyny.x), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegZ, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.y = uint(lerp(float(skyvis_pznzpyny.y), 255.0f * faceSkyVis, currWeight));

            sunDirect *= 1.0f / 6.0f;
            skyEmissiveDirect *= 1.0f / 6.0f;
            Cubes[cubeIdx].SunDirect = LerpPackedColorAlt(Cubes[cubeIdx].SunDirect, sunDirect, currWeight);
            Cubes[cubeIdx].SkyEmissiveDirect =
                LerpPackedColorAlt(Cubes[cubeIdx].SkyEmissiveDirect, skyEmissiveDirect, currWeight);
            AdvanceAmbientCubeSampleCount(skyvis_pxnxs0s1, sampleCount);
            Cubes[cubeIdx].SkyVisibilityPZNZPYNY = PackBytes(skyvis_pznzpyny);
            Cubes[cubeIdx].SkyVisibilityPXNXS0S1 = PackBytes(skyvis_pxnxs0s1);
        }
    }

    public void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        uint4 unpacked = UnpackNibblesLow(Voxels[voxelIdx].distanceToSolid);
        if (unpacked.x < 15 && cubeIdx >= 0) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            uint iterate = Bindless.GetGpuscene().Camera[0].TotalFrames;

            uint bakeCascadeIndex = Bindless.GetGpuscene().CustomData1;
            uint4 skyvis_pznzpyny = UnpackBytes(Cubes[cubeIdx].SkyVisibilityPZNZPYNY);
            uint4 skyvis_pxnxs0s1 = UnpackBytes(Cubes[cubeIdx].SkyVisibilityPXNXS0S1);
            uint sampleCount = skyvis_pxnxs0s1.z;
            float currWeight = AmbientCubeAccumulationWeight(sampleCount);
            float4 faceSun;
            float4 faceSkyEmissive;
            float faceSkyVis;
            float4 sunDirect = float4(0.0f);
            float4 skyEmissiveDirect = float4(0.0f);

            FaceTask(tracer, origin, float3(0, 1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosY, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.z = uint(lerp(float(skyvis_pznzpyny.z), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegY, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.w = uint(lerp(float(skyvis_pznzpyny.w), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosX, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pxnxs0s1.x = uint(lerp(float(skyvis_pxnxs0s1.x), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegX, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pxnxs0s1.y = uint(lerp(float(skyvis_pxnxs0s1.y), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosZ, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.x = uint(lerp(float(skyvis_pznzpyny.x), 255.0f * faceSkyVis, currWeight));
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegZ, faceSun, faceSkyEmissive, faceSkyVis, currWeight);
            sunDirect += faceSun; skyEmissiveDirect += faceSkyEmissive;
            skyvis_pznzpyny.y = uint(lerp(float(skyvis_pznzpyny.y), 255.0f * faceSkyVis, currWeight));

            sunDirect *= 1.0f / 6.0f;
            skyEmissiveDirect *= 1.0f / 6.0f;
            Cubes[cubeIdx].SunDirect = LerpPackedColorAlt(Cubes[cubeIdx].SunDirect, sunDirect, currWeight);
            Cubes[cubeIdx].SkyEmissiveDirect =
                LerpPackedColorAlt(Cubes[cubeIdx].SkyEmissiveDirect, skyEmissiveDirect, currWeight);
            AdvanceAmbientCubeSampleCount(skyvis_pxnxs0s1, sampleCount);
            Cubes[cubeIdx].SkyVisibilityPZNZPYNY = PackBytes(skyvis_pznzpyny);
            Cubes[cubeIdx].SkyVisibilityPXNXS0S1 = PackBytes(skyvis_pxnxs0s1);
        }
    }
}
