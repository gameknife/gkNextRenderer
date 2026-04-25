implementing Common;
#include "PreProcessor.slang"

public static const int SHADOWMAP_SIZE = 4096;

public static const int PAGE_COUNT = 64;  // 64x64
public static const float PAGE_SIZE = 16; // 16m
public static const float3 PAGE_OFFSET = float3(-512, 0, -512);

public static const int CUBE_SIZE_XY = 192; // 256;
public static const int CUBE_SIZE_Z = 48;
public static const uint CUBE_CASCADE_MAX = 4;

float GetAmbientCubeUnit()
{
    return max(Bindless.GetGpuscene().Camera[0].AmbientCubeUnit, 0.0001f);
}

float3 GetAmbientCubeOffset()
{
    return Bindless.GetGpuscene().Camera[0].AmbientCubeOffset;
}

public uint GetAmbientCubeCascadeCount()
{
    uint count = uint(max(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.x, 1.0f));
    return min(count, CUBE_CASCADE_MAX);
}

public float GetAmbientCubeCascadeRatio()
{
    return max(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.y, 1.0f);
}

public float3 GetAmbientCubeOffsetBias()
{
    float3 baseOffset = float3(-CUBE_SIZE_XY / 2.0f, -1.375f, -CUBE_SIZE_XY / 2.0f) * GetAmbientCubeUnit();
    return GetAmbientCubeOffset() - baseOffset;
}

public float GetAmbientCubeUnitAtCascade(uint cascadeIndex)
{
    return GetAmbientCubeUnit() * pow(GetAmbientCubeCascadeRatio(), float(cascadeIndex));
}

public float3 GetAmbientCubeOffsetAtCascade(uint cascadeIndex)
{
    float unit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    return float3(-CUBE_SIZE_XY / 2.0f, -1.375f, -CUBE_SIZE_XY / 2.0f) * unit + GetAmbientCubeOffsetBias();
}

public uint GetAmbientCubeCascadeAtPosition(float3 worldPos)
{
    uint cascadeCount = GetAmbientCubeCascadeCount();
    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        float unit = GetAmbientCubeUnitAtCascade(cascadeIndex);
        float3 offset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
        float3 voxelPos = (worldPos - offset) / unit;
        if (voxelPos.x >= 0 && voxelPos.y >= 0 && voxelPos.z >= 0 && voxelPos.x <= CUBE_SIZE_XY - 1 &&
            voxelPos.y <= CUBE_SIZE_Z - 1 && voxelPos.z <= CUBE_SIZE_XY - 1)
        {
            return cascadeIndex;
        }
    }
    return cascadeCount - 1;
}

public float GetAmbientCubeUnitAtPosition(float3 worldPos)
{
    return GetAmbientCubeUnitAtCascade(GetAmbientCubeCascadeAtPosition(worldPos));
}

public uint GetCascadeBaseOffset(uint cascadeIndex)
{
    return cascadeIndex * uint(CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z);
}

public static const float3 cubeVectors[6] = {
    float3(0, 1, 0),
    float3(0, -1, 0),
    float3(0, 0, 1),
    float3(0, 0, -1),
    float3(1, 0, 0),
    float3(-1, 0, 0),
};

public static const uint FACE_TRACING = 16;
public static const uint FACE_TRACING_FAST = 9;
public static const uint FACE_TRACING_SUPER = 4;

const static float2 grid2x2[4] = {
    float2(-0.5, -0.5), float2(0.5, -0.5),
    float2(-0.5, 0.5), float2(0.5, 0.5)
};

const static float2 grid3x3[9] = {
    float2(-0.667, -0.667), float2(0.0, -0.667), float2(0.667, -0.667),
    float2(-0.667, 0.0), float2(0.0, 0.0), float2(0.667, 0.0),
    float2(-0.667, 0.667), float2(0.0, 0.667), float2(0.667, 0.667)
};

const static float2 grid4x4[16] = {
    float2(-0.75, -0.75), float2(-0.25, -0.75), float2(0.25, -0.75), float2(0.75, -0.75),
    float2(-0.75, -0.25), float2(-0.25, -0.25), float2(0.25, -0.25), float2(0.75, -0.25),
    float2(-0.75, 0.25), float2(-0.25, 0.25), float2(0.25, 0.25), float2(0.75, 0.25),
    float2(-0.75, 0.75), float2(-0.25, 0.75), float2(0.25, 0.75), float2(0.75, 0.75)
};

const static float2 grid5x5[25] = {
    float2(-0.8, -0.8), float2(-0.4, -0.8), float2(0.0, -0.8), float2(0.4, -0.8), float2(0.8, -0.8),
    float2(-0.8, -0.4), float2(-0.4, -0.4), float2(0.0, -0.4), float2(0.4, -0.4), float2(0.8, -0.4),
    float2(-0.8, 0.0), float2(-0.4, 0.0), float2(0.0, 0.0), float2(0.4, 0.0), float2(0.8, 0.0),
    float2(-0.8, 0.4), float2(-0.4, 0.4), float2(0.0, 0.4), float2(0.4, 0.4), float2(0.8, 0.4),
    float2(-0.8, 0.8), float2(-0.4, 0.8), float2(0.0, 0.8), float2(0.4, 0.8), float2(0.8, 0.8)
};

uint PackColor(float4 source)
{
    return packRGB10A2(source);
}

float4 UnpackColor(uint packed)
{
    return unpackRGB10A2(packed);
}

uint LerpPackedColor(uint c0, uint c1, float t)
{
    // Extract RGBA components
    float4 color0 = UnpackColor(c0);
    float4 color1 = UnpackColor(c1);
    // Pack back to uint32
    return PackColor(lerp(color0, color1, t));
}

uint LerpPackedColorAlt(uint c0, float4 c1, float t)
{
    // Extract RGBA components
    float4 color0 = UnpackColor(c0);
    float4 color1 = c1;
    // Pack back to uint32
    return PackColor(lerp(color0, color1, t));
}

float4 sampleAmbientCubeHL2_ID(AmbientCube cube, float3 normal)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX);
    color += wnx * UnpackColor(cube.NegX);
    color += wy * UnpackColor(cube.PosY);
    color += wny * UnpackColor(cube.NegY);
    color += wz * UnpackColor(cube.PosZ);
    color += wnz * UnpackColor(cube.NegZ);

    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return color;
}

float4 sampleAmbientCubeHL2_DI(AmbientCube cube, float3 normal)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX_D);
    color += wnx * UnpackColor(cube.NegX_D);
    color += wy * UnpackColor(cube.PosY_D);
    color += wny * UnpackColor(cube.NegY_D);
    color += wz * UnpackColor(cube.PosZ_D);
    color += wnz * UnpackColor(cube.NegZ_D);

    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return color;
}

float4 sampleAmbientCubeHL2_LightCache(AmbientCube cube, float3 normal)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX_D);
    color += wnx * UnpackColor(cube.NegX_D);
    color += wy * UnpackColor(cube.PosY_D);
    color += wny * UnpackColor(cube.NegY_D);
    color += wz * UnpackColor(cube.PosZ_D);
    color += wnz * UnpackColor(cube.NegZ_D);

    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return color;
}

float4 sampleAmbientCubeHL2_Full(AmbientCube cube, float3 normal)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float skyvis = 0.0;
    float sunvis = 0.0;
    float sum = 0.0;

    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);

    sum = wx + wnx + wy + wny + wz + wnz;

    uint4 unpacked_skyvis0 = UnpackBytes(cube.skyVisibility_pznzpyny);
    uint4 unpacked_skyvis1 = UnpackBytes(cube.skyVisibility_pxnxs0s1);

    skyvis += unpacked_skyvis0.x * wz;
    skyvis += unpacked_skyvis0.y * wnz;
    skyvis += unpacked_skyvis0.z * wy;
    skyvis += unpacked_skyvis0.w * wny;
    skyvis += unpacked_skyvis1.x * wx;
    skyvis += unpacked_skyvis1.y * wnx;

    color += wx * UnpackColor(cube.PosX_D);
    color += wnx * UnpackColor(cube.NegX_D);
    color += wy * UnpackColor(cube.PosY_D);
    color += wny * UnpackColor(cube.NegY_D);
    color += wz * UnpackColor(cube.PosZ_D);
    color += wnz * UnpackColor(cube.NegZ_D);

    color += wx * UnpackColor(cube.PosX);
    color += wnx * UnpackColor(cube.NegX);
    color += wy * UnpackColor(cube.PosY);
    color += wny * UnpackColor(cube.NegY);
    color += wz * UnpackColor(cube.PosZ);
    color += wnz * UnpackColor(cube.NegZ);

    // Normalize
    color.xyz *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    skyvis *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    color.a = skyvis / 255.0f;
    return color;
}

interface IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal);
}




public struct FullAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_Full(cube, normal);
    }
};

public struct DIAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_DI(cube, normal);
    }
};

public struct LightCacheAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_LightCache(cube, normal);
    }
};

public struct IDAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_ID(cube, normal);
    }
};

static const float kTraceHistoryLength = 16.0f;

AmbientCube FetchCube(int3 probePos, in AmbientCube* Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY + probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

AmbientCube FetchCube(int3 probePos, in AmbientCube* Cubes, uint baseOffset)
{
    int idx = int(baseOffset) + probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY + probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

AmbientCube FetchCubeStable(int3 probePos, in AmbientCube* Cubes, in AmbientCube* CubesPong, uint baseOffset,
                              uint cascadeIndex, uint stableCascadeIndex)
{
    if (cascadeIndex == stableCascadeIndex)
    {
        return FetchCube(probePos, CubesPong);
    }
    return FetchCube(probePos, Cubes, baseOffset);
}

VoxelData FetchVoxel(int3 probePos, in VoxelData* Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY + probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

VoxelData FetchVoxel(int3 probePos, in VoxelData* Cubes, uint baseOffset)
{
    int idx = int(baseOffset) + probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY + probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

public float4 interpolateAmbientCubes<T : IAmbientCubeSampler>(float3 inPos, float3 normal)
{
    AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    uint cascadeCount = GetAmbientCubeCascadeCount();

    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
        float3 cubeOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
        float3 nearpos = (inPos - cubeOffset) / cubeUnit;

        if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 || nearpos.x > CUBE_SIZE_XY - 2 || nearpos.y > CUBE_SIZE_Z - 2 ||
            nearpos.z > CUBE_SIZE_XY - 2)
        {
            continue;
        }

        uint baseOffset = GetCascadeBaseOffset(cascadeIndex);
        int3 baseIdx = int3(floor(nearpos));
        float3 frac = frac(nearpos);

        float totalWeight = 0.0;
        float4 result = float4(0.0, 0.0, 0.0, 0.0);

        for (int i = 0; i < 8; i++)
        {
            int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);

            int3 probePos = baseIdx + offset;
            AmbientCube cube = FetchCube(probePos, Cubes, baseOffset);
            VoxelData voxel = FetchVoxel(probePos, Voxels, baseOffset);

            uint4 unpacked0 = UnpackBytes(voxel.distanceToSolid_gg_z01);
            uint4 unpacked1 = UnpackBytes(voxel.distanceToSolid_x01_y01);

            float4 distance00z01 = unpacked0 / 255.0f;
            float4 distancex01y01 = unpacked1 / 255.0f;

            if (distance00z01.y < 0.01f)
                continue;

            float3 localPos = frac;
            float3 probeRealPos = offset;
            float3 probeToLocal = localPos - probeRealPos;

            float distanceToProbe = length(probeToLocal);
            float3 dir = normalize(probeToLocal);

            float distPZ = distance00z01.z;
            float distNZ = distance00z01.w;
            float distPX = distancex01y01.x;
            float distNX = distancex01y01.y;
            float distPY = distancex01y01.z;
            float distNY = distancex01y01.w;

            float wx_pos = sqrt(max(dir.x, 0.0));
            float wx_neg = sqrt(max(-dir.x, 0.0));
            float wy_pos = sqrt(max(dir.y, 0.0));
            float wy_neg = sqrt(max(-dir.y, 0.0));
            float wz_pos = sqrt(max(dir.z, 0.0));
            float wz_neg = sqrt(max(-dir.z, 0.0));

            float hitLength = wx_pos * distPX + wx_neg * distNX + wy_pos * distPY + wy_neg * distNY +
                              wz_pos * distPZ + wz_neg * distNZ;

            if (distanceToProbe > hitLength + 0.05f)
                continue;

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;

            float4 sampleColor = T.Sample(cube, normal);
            result += sampleColor * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0)
        {
            return result / totalWeight;
        }
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

public float4 interpolateAmbientCubesStable<T : IAmbientCubeSampler>(float3 inPos, float3 normal, uint stableCascadeIndex)
{
    AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
    AmbientCube* CubesPong = Bindless.GetGpuscene().CubesPong;
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    uint cascadeCount = GetAmbientCubeCascadeCount();

    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
        float3 cubeOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
        float3 nearpos = (inPos - cubeOffset) / cubeUnit;

        if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 || nearpos.x > CUBE_SIZE_XY - 2 || nearpos.y > CUBE_SIZE_Z - 2 ||
            nearpos.z > CUBE_SIZE_XY - 2)
        {
            continue;
        }

        uint baseOffset = GetCascadeBaseOffset(cascadeIndex);
        int3 baseIdx = int3(floor(nearpos));
        float3 frac = frac(nearpos);

        float totalWeight = 0.0;
        float4 result = float4(0.0, 0.0, 0.0, 0.0);

        for (int i = 0; i < 8; i++)
        {
            int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);

            int3 probePos = baseIdx + offset;
            AmbientCube cube = FetchCubeStable(probePos, Cubes, CubesPong, baseOffset, cascadeIndex, stableCascadeIndex);
            VoxelData voxel = FetchVoxel(probePos, Voxels, baseOffset);

            uint4 unpacked0 = UnpackBytes(voxel.distanceToSolid_gg_z01);
            uint4 unpacked1 = UnpackBytes(voxel.distanceToSolid_x01_y01);

            float4 distance00z01 = unpacked0 / 255.0f;
            float4 distancex01y01 = unpacked1 / 255.0f;

            if (distance00z01.y < 0.01f)
                continue;

            float3 localPos = frac;
            float3 probeRealPos = offset;
            float3 probeToLocal = localPos - probeRealPos;

            float distanceToProbe = length(probeToLocal);
            float3 dir = normalize(probeToLocal);

            float distPZ = distance00z01.z;
            float distNZ = distance00z01.w;
            float distPX = distancex01y01.x;
            float distNX = distancex01y01.y;
            float distPY = distancex01y01.z;
            float distNY = distancex01y01.w;

            float wx_pos = sqrt(max(dir.x, 0.0));
            float wx_neg = sqrt(max(-dir.x, 0.0));
            float wy_pos = sqrt(max(dir.y, 0.0));
            float wy_neg = sqrt(max(-dir.y, 0.0));
            float wz_pos = sqrt(max(dir.z, 0.0));
            float wz_neg = sqrt(max(-dir.z, 0.0));

            float hitLength = wx_pos * distPX + wx_neg * distNX + wy_pos * distPY + wy_neg * distNY +
                              wz_pos * distPZ + wz_neg * distNZ;

            if (distanceToProbe > hitLength + 0.05f)
                continue;

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;

            float4 sampleColor = T.Sample(cube, normal);
            result += sampleColor * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0)
        {
            return result / totalWeight;
        }
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float skipSetp,
                      in VoxelData* Cubes, uint baseOffset, float cubeUnit)
{
    skipSetp = 1;
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 2 || pos.y > CUBE_SIZE_Z - 2 || pos.z > CUBE_SIZE_XY - 2)
    {
        skipSetp = 1;
        return false;
    }

    int3 baseIdx = int3(floor(pos));

    VoxelData cube = FetchVoxel(baseIdx, Cubes, baseOffset);
    matId = cube.matId;

    uint4 unpack0 =UnpackBytes(cube.distanceToSolid_gg_z01);
    uint4 unpack1 = UnpackBytes(cube.distanceToSolid_x01_y01);
    if (cube.matId > 0)
    {
        if (unpack0.y == 0)
        {
            offsetDist = 0;
            return true;
        }

        float3 dir = normalize(-rayDir);

        float distPZ = unpack0.z / 255.0f;
        float distNZ = unpack0.w / 255.0f;
        float distPX = unpack1.x / 255.0f;
        float distNX = unpack1.y / 255.0f;
        float distPY = unpack1.z / 255.0f;
        float distNY = unpack1.w / 255.0f;

        float wx_pos = (max(dir.x, 0.0));
        float wx_neg = (max(-dir.x, 0.0));
        float wy_pos = (max(dir.y, 0.0));
        float wy_neg = (max(-dir.y, 0.0));
        float wz_pos = (max(dir.z, 0.0));
        float wz_neg = (max(-dir.z, 0.0));
        float hitLength = wx_pos * distPX + wx_neg * distNX +
                          wy_pos * distPY + wy_neg * distNY +
                          wz_pos * distPZ + wz_neg * distNZ;

        float hitLengthRef = wx_pos + wx_neg +
                          wy_pos + wy_neg +
                          wz_pos + wz_neg;

        if (hitLength < hitLengthRef)
        {
            offsetDist = hitLength * cubeUnit;
            return true;
        }
    }

    if (unpack0.x > 1)
    {
        skipSetp = max(1, unpack0.x);
    }

    return false;
}

public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float skipSetp,
                      in VoxelData* Cubes)
{
    return inSolid(pos, rayDir, offsetDist, matId, skipSetp, Cubes, 0, GetAmbientCubeUnit());
}

public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float skipSetp,
                      in VoxelData* Cubes, uint baseOffset)
{
    return inSolid(pos, rayDir, offsetDist, matId, skipSetp, Cubes, baseOffset, GetAmbientCubeUnit());
}

public interface IProbeGenerator
{
    void Render(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed);
    void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed);
}

// Axis-face indexing convention for propagation:
// 0:+Y  1:-Y  2:+Z  3:-Z  4:+X  5:-X
// Opposite face index = face ^ 1
uint GetFaceDirectByIdx(AmbientCube cube, uint faceIdx)
{
    if (faceIdx == 0) return cube.PosY_D;
    if (faceIdx == 1) return cube.NegY_D;
    if (faceIdx == 2) return cube.PosZ_D;
    if (faceIdx == 3) return cube.NegZ_D;
    if (faceIdx == 4) return cube.PosX_D;
    return cube.NegX_D;
}

uint GetFaceIndirectByIdx(AmbientCube cube, uint faceIdx)
{
    if (faceIdx == 0) return cube.PosY;
    if (faceIdx == 1) return cube.NegY;
    if (faceIdx == 2) return cube.PosZ;
    if (faceIdx == 3) return cube.NegZ;
    if (faceIdx == 4) return cube.PosX;
    return cube.NegX;
}

float GetVoxelFaceVisibilityByIdx(VoxelData voxel, uint faceIdx)
{
    uint4 unpacked0 = UnpackBytes(voxel.distanceToSolid_gg_z01);
    uint4 unpacked1 = UnpackBytes(voxel.distanceToSolid_x01_y01);

    if (faceIdx == 0) return float(unpacked1.z) / 255.0f;
    if (faceIdx == 1) return float(unpacked1.w) / 255.0f;
    if (faceIdx == 2) return float(unpacked0.z) / 255.0f;
    if (faceIdx == 3) return float(unpacked0.w) / 255.0f;
    if (faceIdx == 4) return float(unpacked1.x) / 255.0f;
    return float(unpacked1.y) / 255.0f;
}

// Bake-side logic (FGpuProbeGenerator + propagation helpers) lives in AmbientCubeBaker.slang.
