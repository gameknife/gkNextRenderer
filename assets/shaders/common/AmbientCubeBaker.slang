implementing Common;
#include "PreProcessor.slang"

// Probe baker for ambient cubes.
// Split out of AmbientCube.slang: AmbientCube.slang owns sampler interfaces and
// runtime fetch helpers; this file owns the bake-side logic that writes back into
// the cube/voxel buffers (FaceTask tracing, distance/inside detection). Tracing
// always bakes direct+indirect: the indirect bounce samples neighbour cubes via
// interpolateAmbientCubesStable, so there is no separate propagation pass.

public struct FGpuProbeGenerator : IProbeGenerator
{
    bool FaceTask(IRayTracer tracer, float3 origin, float3 basis, uint iterate, uint bakeCascadeIndex, inout uint DirectLight, inout uint IndirectLight, inout uint SkyVisOut, inout uint SunVisOut)
    {
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];
        float cubeUnit = GetAmbientCubeUnit();

        origin = origin + basis * cubeUnit * 0.25f;
        // 输出结果
        float4 directColor = float4(0.0);
        float4 bounceColor = float4(0.0);

        float skyVisibility = 0.0;

        // 抖动
        const float2 offset = grid3x3[iterate % 9] * 0.25f;

        // 这个采样还是有点过重，可以考虑用probe间的光能传递来完成，而非直接tracing
        for (uint i = 0; i < FACE_TRACING; i++)
        {
            float3 hemiVec = normalize(float3(grid4x4[i] + offset, 1.0));
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
                    directColor += outAlbedo;
                }
                else
                {
                    bounceColor += outAlbedo * interpolateAmbientCubesStable<DIAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, bakeCascadeIndex) * 1.25f; // magic bounce twice
                }
            }
            else
            {
                directColor += Common.SampleIBLRough(Camera.SkyIdx, rayDir, Camera.SkyRotation) * (Camera.HasSky ? Camera.SkyIntensity : 0.0f);
                skyVisibility += 1.0f;
            }
        }

        directColor = directColor / float(FACE_TRACING);
        bounceColor = bounceColor / float(FACE_TRACING);

        // // 太阳光
        if (Camera.HasSun)
        {
            float3 sunDir = Camera.SunDirection.xyz;
            float sunAtten = tracer.TraceOcclusion(origin, sunDir) ? 0.0f : 1.0f;
            float ndotl = clamp(dot(basis, sunDir), 0.0f, 1.0f);
            SunVisOut = sunAtten > 0.0f ? 1 : 0;
            directColor += Camera.SunColor * sunAtten * ndotl * (Camera.HasSun ? 1.0f : 0.0f) * 0.25f;
        }

        // 累积，gpu直接只保留4帧
        float accumulatedFrames = min(float(iterate) + 1.0f, kTraceHistoryLength);
        float currWeight = 1.0f / accumulatedFrames;
        SkyVisOut = uint(lerp(float(SkyVisOut), 255.0 * skyVisibility / float(FACE_TRACING), currWeight));
        DirectLight = LerpPackedColorAlt(DirectLight, directColor, currWeight);
        IndirectLight = LerpPackedColorAlt(IndirectLight, bounceColor, currWeight);
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

        // store the dist, the dist is abs meters, can store 4 unit_size max
        Voxels[voxelIdx].distanceToSolid_gg_z01 = PackBytes(uint4(uint(minDist / cubeUnit), uint(inside * 255.0f), uint(distPZ * 255.0f), uint(distNZ * 255.0f)));
        Voxels[voxelIdx].distanceToSolid_x01_y01 = PackBytes(uint4(uint(distPX * 255.0f), uint(distNX * 255.0f), uint(distPY * 255.0f), uint(distNY * 255.0f)));

        if (minDist < 8 && cubeIdx >= 0) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            // Only touch the low 24 bits; the high byte holds the voxel sky-visibility baked by the
            // NoAmbient path and must not be polluted by the cube baker's counter (design §5.5).
            uint iterate = GetVoxelAgeCounter(Voxels[voxelIdx]);
            SetVoxelAgeCounter(Voxels[voxelIdx], iterate + 1);

            uint4 skyvis_pznzpynyx = UnpackBytes(Cubes[cubeIdx].skyVisibility_pznzpyny);
            uint4 skyvis_pxnxs0s1 = UnpackBytes(Cubes[cubeIdx].skyVisibility_pxnxs0s1);

            uint sunvis = 0;

            uint bakeCascadeIndex = Bindless.GetGpuscene().custom_data_1;
            FaceTask(tracer, origin, float3(0, 1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosY_D, Cubes[cubeIdx].PosY, skyvis_pznzpynyx.z, skyvis_pxnxs0s1.z);
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegY_D, Cubes[cubeIdx].NegY, skyvis_pznzpynyx.w, sunvis);
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosX_D, Cubes[cubeIdx].PosX, skyvis_pxnxs0s1.x, sunvis);
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegX_D, Cubes[cubeIdx].NegX, skyvis_pxnxs0s1.y, sunvis);
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosZ_D, Cubes[cubeIdx].PosZ, skyvis_pznzpynyx.x, sunvis);
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegZ_D, Cubes[cubeIdx].NegZ, skyvis_pznzpynyx.y, sunvis);

            Cubes[cubeIdx].skyVisibility_pznzpyny = PackBytes(skyvis_pznzpynyx);
            Cubes[cubeIdx].skyVisibility_pxnxs0s1 = PackBytes(skyvis_pxnxs0s1);
        }
    }

    public void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        uint4 upacked = UnpackBytes(Voxels[voxelIdx].distanceToSolid_gg_z01);
        if (upacked.x < 16 && cubeIdx >= 0) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            // Counter lives in the low 24 bits; preserve the sky-visibility high byte (design §5.5).
            uint iterate = GetVoxelAgeCounter(Voxels[voxelIdx]);
            SetVoxelAgeCounter(Voxels[voxelIdx], iterate + 1);

            uint4 skyvis_pznzpynyx = UnpackBytes(Cubes[cubeIdx].skyVisibility_pznzpyny);
            uint4 skyvis_pxnxs0s1 = UnpackBytes(Cubes[cubeIdx].skyVisibility_pxnxs0s1);

            uint sunvis = 0;

            uint bakeCascadeIndex = Bindless.GetGpuscene().custom_data_1;
            FaceTask(tracer, origin, float3(0, 1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosY_D, Cubes[cubeIdx].PosY, skyvis_pznzpynyx.z, skyvis_pxnxs0s1.z);
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegY_D, Cubes[cubeIdx].NegY, skyvis_pznzpynyx.w, sunvis);
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosX_D, Cubes[cubeIdx].PosX, skyvis_pxnxs0s1.x, sunvis);
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegX_D, Cubes[cubeIdx].NegX, skyvis_pxnxs0s1.y, sunvis);
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, bakeCascadeIndex, Cubes[cubeIdx].PosZ_D, Cubes[cubeIdx].PosZ, skyvis_pznzpynyx.x, sunvis);
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, bakeCascadeIndex, Cubes[cubeIdx].NegZ_D, Cubes[cubeIdx].NegZ, skyvis_pznzpynyx.y, sunvis);

            Cubes[cubeIdx].skyVisibility_pznzpyny = PackBytes(skyvis_pznzpynyx);
            Cubes[cubeIdx].skyVisibility_pxnxs0s1 = PackBytes(skyvis_pxnxs0s1);
        }
    }
}
