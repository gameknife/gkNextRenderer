implementing Common;
#include "PreProcessor.slang"

// Probe baker / propagation pass for ambient cubes.
// Split out of AmbientCube.slang: AmbientCube.slang owns sampler interfaces and
// runtime fetch helpers; this file owns the bake-side logic that writes back into
// the cube/voxel buffers (FaceTask tracing, distance/inside detection, propagation
// gather + emissive injection).

static const float kPropagationAirTransport = 0.95f;
static const float kPropagationSurfaceTransport = 0.9f;
static const float kPropagationCrossFaceMix = 0.35f;

float3 GetIndirectAverage(AmbientCube cube)
{
    float3 sum = float3(0.0);
    sum += UnpackColor(cube.PosY).rgb;
    sum += UnpackColor(cube.NegY).rgb;
    sum += UnpackColor(cube.PosZ).rgb;
    sum += UnpackColor(cube.NegZ).rgb;
    sum += UnpackColor(cube.PosX).rgb;
    sum += UnpackColor(cube.NegX).rgb;
    return sum * (1.0f / 6.0f);
}

public struct FGpuProbeGenerator : IProbeGenerator
{
    bool FaceTask(IRayTracer tracer, float3 origin, float3 basis, uint iterate, uint bakeCascadeIndex, bool directOnly, inout uint DirectLight, inout uint IndirectLight, inout uint SkyVisOut, inout uint SunVisOut)
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
                else if (!directOnly)
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
        if (!directOnly)
        {
            IndirectLight = LerpPackedColorAlt(IndirectLight, bounceColor, currWeight);
        }
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

    public void Render(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed)
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
        // Voxels[CubeIdx].matId = 0;
        // InsideGeometry(tracer, origin, float3(0, 1, 0), Voxels[CubeIdx].matId, distPY);
        // InsideGeometry(tracer, origin, float3(0, -1, 0), Voxels[CubeIdx].matId, distNY);
        // InsideGeometry(tracer, origin, float3(1, 0, 0), Voxels[CubeIdx].matId, distPX);
        // InsideGeometry(tracer, origin, float3(-1, 0, 0), Voxels[CubeIdx].matId, distNX);
        // InsideGeometry(tracer, origin, float3(0, 0, 1), Voxels[CubeIdx].matId, distPZ);
        // InsideGeometry(tracer, origin, float3(0, 0, -1), Voxels[CubeIdx].matId, distNZ);

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
        Voxels[CubeIdx].distanceToSolid_gg_z01 = PackBytes(uint4(uint(minDist / cubeUnit), uint(inside * 255.0f), uint(distPZ * 255.0f), uint(distNZ * 255.0f)));
        Voxels[CubeIdx].distanceToSolid_x01_y01 = PackBytes(uint4(uint(distPX * 255.0f), uint(distNX * 255.0f), uint(distPY * 255.0f), uint(distNY * 255.0f)));

        if (minDist < 8) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            uint iterate = Voxels[CubeIdx].age;
            Voxels[CubeIdx].age = Voxels[CubeIdx].age + 1;

            uint4 skyvis_pznzpynyx = UnpackBytes(Cubes[CubeIdx].skyVisibility_pznzpyny);
            uint4 skyvis_pxnxs0s1 = UnpackBytes(Cubes[CubeIdx].skyVisibility_pxnxs0s1);

            uint sunvis = 0;

            uint bakeCascadeIndex = Bindless.GetGpuscene().custom_data_1;
            bool directOnly = Bindless.GetGpuscene().custom_data_2 != 0u;
            FaceTask(tracer, origin, float3(0, 1, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].PosY_D, Cubes[CubeIdx].PosY, skyvis_pznzpynyx.z, skyvis_pxnxs0s1.z);
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].NegY_D, Cubes[CubeIdx].NegY, skyvis_pznzpynyx.w, sunvis);
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].PosX_D, Cubes[CubeIdx].PosX, skyvis_pxnxs0s1.x, sunvis);
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].NegX_D, Cubes[CubeIdx].NegX, skyvis_pxnxs0s1.y, sunvis);
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].PosZ_D, Cubes[CubeIdx].PosZ, skyvis_pznzpynyx.x, sunvis);
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].NegZ_D, Cubes[CubeIdx].NegZ, skyvis_pznzpynyx.y, sunvis);

            Cubes[CubeIdx].skyVisibility_pznzpyny = PackBytes(skyvis_pznzpynyx);
            Cubes[CubeIdx].skyVisibility_pxnxs0s1 = PackBytes(skyvis_pxnxs0s1);
        }
    }

    public void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        uint4 upacked = UnpackBytes(Voxels[CubeIdx].distanceToSolid_gg_z01);
        if (upacked.x < 16) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            uint iterate = Voxels[CubeIdx].age;
            Voxels[CubeIdx].age = Voxels[CubeIdx].age + 1;

            uint4 skyvis_pznzpynyx = UnpackBytes(Cubes[CubeIdx].skyVisibility_pznzpyny);
            uint4 skyvis_pxnxs0s1 = UnpackBytes(Cubes[CubeIdx].skyVisibility_pxnxs0s1);

            uint sunvis = 0;

            uint bakeCascadeIndex = Bindless.GetGpuscene().custom_data_1;
            bool directOnly = Bindless.GetGpuscene().custom_data_2 != 0u;
            FaceTask(tracer, origin, float3(0, 1, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].PosY_D, Cubes[CubeIdx].PosY, skyvis_pznzpynyx.z, skyvis_pxnxs0s1.z);
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].NegY_D, Cubes[CubeIdx].NegY, skyvis_pznzpynyx.w, sunvis);
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].PosX_D, Cubes[CubeIdx].PosX, skyvis_pxnxs0s1.x, sunvis);
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].NegX_D, Cubes[CubeIdx].NegX, skyvis_pxnxs0s1.y, sunvis);
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].PosZ_D, Cubes[CubeIdx].PosZ, skyvis_pznzpynyx.x, sunvis);
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, bakeCascadeIndex, directOnly, Cubes[CubeIdx].NegZ_D, Cubes[CubeIdx].NegZ, skyvis_pznzpynyx.y, sunvis);

            Cubes[CubeIdx].skyVisibility_pznzpyny = PackBytes(skyvis_pznzpynyx);
            Cubes[CubeIdx].skyVisibility_pxnxs0s1 = PackBytes(skyvis_pxnxs0s1);
        }
    }

    // Gather the bounce contribution arriving at a voxel face from the axis neighbor in that face direction.
    // The propagation pass updates only the bounce channel and keeps traced primary lighting untouched.
    float3 GatherPropagationFace(int3 localPos, uint faceIdx, uint cascadeBase, VoxelData V_data)
    {
        AmbientCube* CubesPong = Bindless.GetGpuscene().CubesPong;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        int3 dir = int3(cubeVectors[faceIdx]);
        int3 neighborPos = localPos + dir;
        float faceVisibility = GetVoxelFaceVisibilityByIdx(V_data, faceIdx);

        if (faceVisibility <= 0.001f)
        {
            return float3(0.0);
        }

        if (neighborPos.x < 0 || neighborPos.x >= CUBE_SIZE_XY ||
            neighborPos.y < 0 || neighborPos.y >= CUBE_SIZE_Z ||
            neighborPos.z < 0 || neighborPos.z >= CUBE_SIZE_XY)
        {
            return float3(0.0);
        }

        uint neighborLocalIdx =
            uint(neighborPos.y) * CUBE_SIZE_XY * CUBE_SIZE_XY +
            uint(neighborPos.z) * CUBE_SIZE_XY +
            uint(neighborPos.x);

        VoxelData N_data = Voxels[cascadeBase + neighborLocalIdx];
        AmbientCube N_prev = CubesPong[neighborLocalIdx];
        uint oppositeFace = faceIdx ^ 1u;

        if (N_data.matId == 0)
        {
            // Empty neighbor: transport the previous indirect field with finite air loss.
            // A small cross-face mix lets wall bounce redistribute without flooding open floors.
            float neighborFaceVisibility = GetVoxelFaceVisibilityByIdx(N_data, oppositeFace);
            float transport = saturate(faceVisibility * neighborFaceVisibility);
            float3 sameFaceIndirect = UnpackColor(GetFaceIndirectByIdx(N_prev, faceIdx)).rgb;
            float3 mixedIndirect = lerp(sameFaceIndirect, GetIndirectAverage(N_prev), kPropagationCrossFaceMix);
            return mixedIndirect * transport * kPropagationAirTransport;
        }

        // Surface neighbor: sample the neighbor cache instead of feeding V back into itself.
        Material mat = Bindless.GetGpuscene().GetMaterial(N_data.matId);
        float3 albedo = mat.Diffuse.rgb;
        float neighborFaceVisibility = GetVoxelFaceVisibilityByIdx(N_data, oppositeFace);
        float transport = saturate(faceVisibility * max(neighborFaceVisibility, 0.25f));
        float3 direct = UnpackColor(GetFaceDirectByIdx(N_prev, oppositeFace)).rgb;
        float3 indirect = UnpackColor(GetFaceIndirectByIdx(N_prev, oppositeFace)).rgb;
        float3 incident = direct + indirect;
        float3 reflected = albedo * incident;
        return reflected * transport * kPropagationSurfaceTransport;
    }

    // Emissive surfaces are injected separately so transport can stay a pure gather step.
    float3 GatherPropagationInjectionFace(int3 localPos, uint faceIdx, uint cascadeBase, VoxelData V_data)
    {
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        int3 dir = int3(cubeVectors[faceIdx]);
        int3 neighborPos = localPos + dir;
        float faceVisibility = GetVoxelFaceVisibilityByIdx(V_data, faceIdx);

        if (faceVisibility <= 0.001f ||
            neighborPos.x < 0 || neighborPos.x >= CUBE_SIZE_XY ||
            neighborPos.y < 0 || neighborPos.y >= CUBE_SIZE_Z ||
            neighborPos.z < 0 || neighborPos.z >= CUBE_SIZE_XY)
        {
            return float3(0.0);
        }

        uint neighborLocalIdx =
            uint(neighborPos.y) * CUBE_SIZE_XY * CUBE_SIZE_XY +
            uint(neighborPos.z) * CUBE_SIZE_XY +
            uint(neighborPos.x);

        VoxelData N_data = Voxels[cascadeBase + neighborLocalIdx];
        if (N_data.matId == 0u)
        {
            return float3(0.0);
        }

        Material mat = Bindless.GetGpuscene().GetMaterial(N_data.matId);
        if (mat.MaterialModel != MaterialDiffuseLight)
        {
            return float3(0.0);
        }

        uint oppositeFace = faceIdx ^ 1u;
        float neighborFaceVisibility = GetVoxelFaceVisibilityByIdx(N_data, oppositeFace);
        float transport = saturate(faceVisibility * max(neighborFaceVisibility, 0.25f));
        return mat.Diffuse.rgb * transport;
    }

    // Propagation-based alternative to tracing bounce: writes only the indirect channels
    // by gathering from 6 axis neighbors in the same cascade. Direct/sky channels remain
    // untouched, populated by the lower-frequency direct tracing bake.
    public void RenderAmbientCubeByPropagation(uint CubeIdx, uint cascadeIndex, int3 localPos)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        uint cascadeBase = GetCascadeBaseOffset(cascadeIndex);

        VoxelData V_data = Voxels[CubeIdx];
        uint4 upacked = UnpackBytes(V_data.distanceToSolid_gg_z01);

        if (upacked.x >= 16u)
        {
            return;
        }

        float3 PY = GatherPropagationFace(localPos, 0u, cascadeBase, V_data);
        float3 NY = GatherPropagationFace(localPos, 1u, cascadeBase, V_data);
        float3 PZ = GatherPropagationFace(localPos, 2u, cascadeBase, V_data);
        float3 NZ = GatherPropagationFace(localPos, 3u, cascadeBase, V_data);
        float3 PX = GatherPropagationFace(localPos, 4u, cascadeBase, V_data);
        float3 NX = GatherPropagationFace(localPos, 5u, cascadeBase, V_data);

        Cubes[CubeIdx].PosY = packRGB10A2(float4(PY, 0.0));
        Cubes[CubeIdx].NegY = packRGB10A2(float4(NY, 0.0));
        Cubes[CubeIdx].PosZ = packRGB10A2(float4(PZ, 0.0));
        Cubes[CubeIdx].NegZ = packRGB10A2(float4(NZ, 0.0));
        Cubes[CubeIdx].PosX = packRGB10A2(float4(PX, 0.0));
        Cubes[CubeIdx].NegX = packRGB10A2(float4(NX, 0.0));
    }

    // Injection seeds the propagation field from adjacent emissive surfaces.
    public void InjectAmbientCubePropagation(uint CubeIdx, uint cascadeIndex, int3 localPos)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        uint cascadeBase = GetCascadeBaseOffset(cascadeIndex);
        VoxelData V_data = Voxels[CubeIdx];
        uint4 upacked = UnpackBytes(V_data.distanceToSolid_gg_z01);

        if (upacked.x >= 16u || V_data.matId != 0u)
        {
            return;
        }

        float3 PY = GatherPropagationInjectionFace(localPos, 0u, cascadeBase, V_data);
        float3 NY = GatherPropagationInjectionFace(localPos, 1u, cascadeBase, V_data);
        float3 PZ = GatherPropagationInjectionFace(localPos, 2u, cascadeBase, V_data);
        float3 NZ = GatherPropagationInjectionFace(localPos, 3u, cascadeBase, V_data);
        float3 PX = GatherPropagationInjectionFace(localPos, 4u, cascadeBase, V_data);
        float3 NX = GatherPropagationInjectionFace(localPos, 5u, cascadeBase, V_data);

        float3 curPY = UnpackColor(Cubes[CubeIdx].PosY).rgb;
        float3 curNY = UnpackColor(Cubes[CubeIdx].NegY).rgb;
        float3 curPZ = UnpackColor(Cubes[CubeIdx].PosZ).rgb;
        float3 curNZ = UnpackColor(Cubes[CubeIdx].NegZ).rgb;
        float3 curPX = UnpackColor(Cubes[CubeIdx].PosX).rgb;
        float3 curNX = UnpackColor(Cubes[CubeIdx].NegX).rgb;

        Cubes[CubeIdx].PosY = packRGB10A2(float4(curPY + PY, 0.0));
        Cubes[CubeIdx].NegY = packRGB10A2(float4(curNY + NY, 0.0));
        Cubes[CubeIdx].PosZ = packRGB10A2(float4(curPZ + PZ, 0.0));
        Cubes[CubeIdx].NegZ = packRGB10A2(float4(curNZ + NZ, 0.0));
        Cubes[CubeIdx].PosX = packRGB10A2(float4(curPX + PX, 0.0));
        Cubes[CubeIdx].NegX = packRGB10A2(float4(curNX + NX, 0.0));
    }
}
