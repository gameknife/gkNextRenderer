implementing Common;
#include "PreProcessor.slang"

#define MAX_ILLUMINANCE 512.f

public static const int SHADOWMAP_SIZE = 4096;

public static const int PAGE_COUNT = 64;  // 64x64
public static const float PAGE_SIZE = 16; // 16m
public static const float3 PAGE_OFFSET = float3(-512, 0, -512);

public static const int CUBE_SIZE_XY = 192; // 256;
public static const int CUBE_SIZE_Z = 48;
public static const float CUBE_UNIT = 0.25f;
public static const float3 CUBE_OFFSET = float3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

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

#define FACE_TRACING_COUNT FACE_TRACING
#define FACE_TRACING_SOURCE grid4x4

uint packRGB10A2(float4 color)
{
    float4 clamped = clamp(color / MAX_ILLUMINANCE, float4(0.0f), float4(1.0f));

    uint r = uint(clamped.r * 1023.0f);
    uint g = uint(clamped.g * 1023.0f);
    uint b = uint(clamped.b * 1023.0f);
    uint a = uint(clamped.a * 3.0f);

    return r | (g << 10) | (b << 20) | (a << 30);
}

float4 unpackRGB10A2(uint packed)
{
    float r = float((packed) & 0x3FF) / 1023.0f;
    float g = float((packed >> 10) & 0x3FF) / 1023.0f;
    float b = float((packed >> 20) & 0x3FF) / 1023.0f;

    return float4(r, g, b, 0.0) * MAX_ILLUMINANCE;
}

// Software implementation to replace hardware-dependent unpack_u8u32
uint4 unpack_bytes(uint packed)
{
    return uint4(
        (packed & 0xFF),
        ((packed >> 8) & 0xFF),
        ((packed >> 16) & 0xFF),
        ((packed >> 24) & 0xFF)
    );
}

// Software implementation to replace hardware-dependent pack_u8
uint pack_bytes(uint4 values)
{
    return (values.x & 0xFF) |
           ((values.y & 0xFF) << 8) |
           ((values.z & 0xFF) << 16) |
           ((values.w & 0xFF) << 24);
}

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

    uint4 unpacked_skyvis0 = unpack_bytes(cube.skyVisibility_pznzpyny);
    uint4 unpacked_skyvis1 = unpack_bytes(cube.skyVisibility_pxnxs0s1);

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

AmbientCube FetchCubeV2(int3 probePos, in AmbientCube* Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
              probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

VoxelData FetchVoxelV2(int3 probePos, in VoxelData* Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
              probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

AmbientCube FetchCube(int3 probePos, in RWStructuredBuffer<AmbientCube> Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
              probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

VoxelData FetchVoxel(int3 probePos, in RWStructuredBuffer<VoxelData> Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
              probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}

AmbientCube RWFetchCube(int3 probePos, in RWStructuredBuffer<AmbientCube> Cubes)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
              probePos.z * CUBE_SIZE_XY + probePos.x;
    return Cubes[idx];
}


public float4 interpolateAmbientCubesV2<T : IAmbientCubeSampler>(float3 inPos, float3 normal, in AmbientCube* Cubes, in VoxelData* Voxels)
{
    float3 nearpos = (inPos - CUBE_OFFSET) / CUBE_UNIT;

    // Early out if position is outside the probe grid
    if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 ||
        nearpos.x > CUBE_SIZE_XY - 1 || nearpos.y > CUBE_SIZE_Z - 1 || nearpos.z > CUBE_SIZE_XY - 1)
    {
        return float4(0, 0, 0, 1);
    }

    int3 baseIdx = int3(floor(nearpos));
    float3 frac = frac(nearpos);

    float totalWeight = 0.0;
    float4 result = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < 8; i++)
    {
        int3 offset = int3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1);

        int3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCubeV2(probePos, Cubes);
        VoxelData voxel = FetchVoxelV2(probePos, Voxels);

        uint4 unpacked0 = unpack_bytes(voxel.distanceToSolid_gg_z01);
        uint4 unpacked1 = unpack_bytes(voxel.distanceToSolid_x01_y01);

        float4 distance00z01 = unpacked0 / 255.0f;
        float4 distancex01y01 = unpacked1 / 255.0f;

        if (distance00z01.y < 0.01f)
            continue;

        float3 localPos = frac;
        float3 probeRealPos = offset;
        float3 probeToLocal = localPos - probeRealPos;

        // 计算查询点到probe的实际距离
        float distanceToProbe = length(probeToLocal);
        
        // 归一化方向向量
        float3 dir = normalize(probeToLocal);
        
        // 从voxel数据中获取各个方向的距离信息
        float distPZ = distance00z01.z;
        float distNZ = distance00z01.w;
        float distPX = distancex01y01.x;
        float distNX = distancex01y01.y;
        float distPY = distancex01y01.z;
        float distNY = distancex01y01.w;
        
        // 根据方向权重混合距离
        float wx_pos = sqrt(max(dir.x, 0.0));
        float wx_neg = sqrt(max(-dir.x, 0.0));
        float wy_pos = sqrt(max(dir.y, 0.0));
        float wy_neg = sqrt(max(-dir.y, 0.0));
        float wz_pos = sqrt(max(dir.z, 0.0));
        float wz_neg = sqrt(max(-dir.z, 0.0));
        
        float hitLength = wx_pos * distPX + wx_neg * distNX +
                            wy_pos * distPY + wy_neg * distNY +
                            wz_pos * distPZ + wz_neg * distNZ;

        // 如果查询点到probe的距离大于插值后的hitLength，说明被遮挡，跳过
        if (distanceToProbe > hitLength + 0.05f)// - 0.125f) 
            continue;

        // voxel here, matId will store at edge, we should discard it by the distance
        // if (voxel.matId > 0)
        //     continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        float4 sampleColor = T.Sample(cube, normal);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // Normalize result by total weight
    float4 indirectColor = totalWeight > 0.0 ? result / totalWeight : float4(0.0, 0.0, 0.0, 0.0);
    return indirectColor;
}

public float4 interpolateAmbientCubes<T : IAmbientCubeSampler>(float3 inPos, float3 normal, in RWStructuredBuffer<AmbientCube> Cubes, in RWStructuredBuffer<VoxelData> Voxels)
{
    float3 nearpos = (inPos - CUBE_OFFSET) / CUBE_UNIT;

    // Early out if position is outside the probe grid
    if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 ||
        nearpos.x > CUBE_SIZE_XY - 1 || nearpos.y > CUBE_SIZE_Z - 1 || nearpos.z > CUBE_SIZE_XY - 1)
    {
        return float4(0, 0, 0, 1);
    }

    int3 baseIdx = int3(floor(nearpos));
    float3 frac = frac(nearpos);

    float totalWeight = 0.0;
    float4 result = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < 8; i++)
    {
        int3 offset = int3(
            i & 1,
            (i >> 1) & 1,
            (i >> 2) & 1);

        int3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos, Cubes);
        VoxelData voxel = FetchVoxel(probePos, Voxels);

        uint4 unpacked0 = unpack_bytes(voxel.distanceToSolid_gg_z01);
        uint4 unpacked1 = unpack_bytes(voxel.distanceToSolid_x01_y01);

        float4 distance00z01 = unpacked0 / 255.0f;
        float4 distancex01y01 = unpacked1 / 255.0f;

        if (distance00z01.y < 0.01f)
            continue;

        float3 localPos = frac;
        float3 probeRealPos = offset;
        float3 probeToLocal = localPos - probeRealPos;

        // 计算查询点到probe的实际距离
        float distanceToProbe = length(probeToLocal);
        
        // 归一化方向向量
        float3 dir = normalize(probeToLocal);
        
        // 从voxel数据中获取各个方向的距离信息
        float distPZ = distance00z01.z;
        float distNZ = distance00z01.w;
        float distPX = distancex01y01.x;
        float distNX = distancex01y01.y;
        float distPY = distancex01y01.z;
        float distNY = distancex01y01.w;
        
        // 根据方向权重混合距离
        float wx_pos = sqrt(max(dir.x, 0.0));
        float wx_neg = sqrt(max(-dir.x, 0.0));
        float wy_pos = sqrt(max(dir.y, 0.0));
        float wy_neg = sqrt(max(-dir.y, 0.0));
        float wz_pos = sqrt(max(dir.z, 0.0));
        float wz_neg = sqrt(max(-dir.z, 0.0));
        
        float hitLength = wx_pos * distPX + wx_neg * distNX +
                            wy_pos * distPY + wy_neg * distNY +
                            wz_pos * distPZ + wz_neg * distNZ;

        // 如果查询点到probe的距离大于插值后的hitLength，说明被遮挡，跳过
        if (distanceToProbe > hitLength + 0.05f)// - 0.125f) 
            continue;

        // voxel here, matId will store at edge, we should discard it by the distance
        // if (voxel.matId > 0)
        //     continue;

        float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
        float weight = wx * wy * wz;

        float occlusion;
        float4 sampleColor = T.Sample(cube, normal);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    // Normalize result by total weight
    float4 indirectColor = totalWeight > 0.0 ? result / totalWeight : float4(0.0, 0.0, 0.0, 0.0);
    return indirectColor;
}

// Interpolate between 8 probes
public float FetchSDF(float3 pos, in RWStructuredBuffer<VoxelData> Cubes)
{
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 1 || pos.y > CUBE_SIZE_Z - 1 || pos.z > CUBE_SIZE_XY - 1)
    {
        return 255.0f;
    }

    int3 baseIdx = int3(floor(pos));
    VoxelData cube = FetchVoxel(baseIdx, Cubes);
    uint4 unpack0 = unpack_bytes(cube.distanceToSolid_gg_z01);
    return unpack0.x;
}

public bool inSolidV2(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float skipSetp, in VoxelData* Cubes)
{
    skipSetp = 1;
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 1 || pos.y > CUBE_SIZE_Z - 1 || pos.z > CUBE_SIZE_XY - 1)
    {
        skipSetp = 1;
        return false;
    }

    int3 baseIdx = int3(floor(pos));

    VoxelData cube = FetchVoxelV2(baseIdx, Cubes);
    matId = cube.matId;

    uint4 unpack0 =unpack_bytes(cube.distanceToSolid_gg_z01);
    uint4 unpack1 = unpack_bytes(cube.distanceToSolid_x01_y01);
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
            offsetDist = hitLength * CUBE_UNIT;
            return true;
        }
    }

    if (unpack0.x > 8)
    {
        skipSetp = max(1, unpack0.x);
    }

    return false;
}
// Interpolate between 8 probes
public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float skipSetp, in RWStructuredBuffer<VoxelData> Cubes)
{
    skipSetp = 1;
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 1 || pos.y > CUBE_SIZE_Z - 1 || pos.z > CUBE_SIZE_XY - 1)
    {
        skipSetp = 1;
        return false;
    }

    int3 baseIdx = int3(floor(pos));

    VoxelData cube = FetchVoxel(baseIdx, Cubes);
    matId = cube.matId;

    uint4 unpack0 =unpack_bytes(cube.distanceToSolid_gg_z01);
    uint4 unpack1 = unpack_bytes(cube.distanceToSolid_x01_y01);
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
            offsetDist = hitLength * CUBE_UNIT;
            return true;
        }
    }

    if (unpack0.x > 8)
    {
        skipSetp = max(1, unpack0.x);
    }

    return false;
}

public interface IProbeGenerator
{
    void Render(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed);
    void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed);
}

public struct FGpuProbeGenerator : IProbeGenerator
{
    LightObject FetchLight(uint lightIdx, inout float4 lightPower)
    {
        LightObject* Lights = Bindless.GetGpuscene().Lights;
        lightPower = Bindless.GetGpuscene().GetMaterial(Lights[lightIdx].lightMatIdx).Diffuse;
        return Lights[lightIdx];
    }

    bool FaceTask(IRayTracer tracer, float3 origin, float3 basis, uint iterate, inout uint DirectLight, inout uint IndirectLight, inout uint SkyVisOut, inout uint SunVisOut)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
        UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

        origin = origin + basis * CUBE_UNIT * 0.25f;
        // 输出结果
        float4 directColor = float4(0.0);
        float4 bounceColor = float4(0.0);

        float skyVisibility = 0.0;
  
        // 抖动
        const float2 offset = grid3x3[iterate % 9] * 0.25f;

        // 这个采样还是有点过重，可以考虑用probe间的光能传递来完成，而非直接tracing
        for (uint i = 0; i < FACE_TRACING_COUNT; i++)
        {
            float3 hemiVec = normalize(float3(FACE_TRACING_SOURCE[i] + offset, 1.0));
            float3 rayDir = AlignWithNormal(hemiVec, basis);

            Vertex hitVertex;
            NodeProxy hitNode;
            if (tracer.TraceRay(origin, rayDir, FAST_MAX_TRACE_DISTANCE, hitVertex, hitNode))
            {
                Material hitMaterial = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
                float4 outAlbedo = hitMaterial.Diffuse;
                if (hitMaterial.DiffuseTextureId >= 0)
                {
                    float4 tex = Bindless.GetSampleTexture(hitMaterial.DiffuseTextureId).SampleLevel(hitVertex.TexCoord, 0);
                    outAlbedo *= tex;
                }
                bounceColor += outAlbedo * interpolateAmbientCubesV2<DIAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, Voxels) * 1.25f; // magic bounce twice
            }
            else
            {
                directColor += Common.SampleIBLRough(Camera.SkyIdx, rayDir, Camera.SkyRotation) * (Camera.HasSky ? Camera.SkyIntensity : 0.0f);
                skyVisibility += 1.0f;
            }
        }

        directColor = directColor / float(FACE_TRACING_COUNT);
        bounceColor = bounceColor / float(FACE_TRACING_COUNT);

        // 参数光源，目前只取了一盏光源
        if (Camera.LightCount > 0)
        {
            float4 lightPower = float4(0.0);
            LightObject light = FetchLight(0, lightPower);
            float3 lightPos = lerp(light.p1.xyz, light.p3.xyz, 0.5f);
            float lightAtten = tracer.TraceSegment(origin, lightPos, CUBE_UNIT * 0.5f) ? 0.0f : 1.0f;
            float3 lightDir = normalize(lightPos - origin);
            float ndotl = clamp(dot(basis, lightDir), 0.0f, 1.0f);
            float distance = length(lightPos - origin);
            float attenuation = ndotl * light.normal_area.w / (distance * distance * 3.14159f);
            directColor += lightPower * attenuation * lightAtten;
        }

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
        float currWeight = 0.125f;
        SkyVisOut = uint(lerp(SkyVisOut, 255 * skyVisibility / float(FACE_TRACING_COUNT), currWeight));
        DirectLight = LerpPackedColorAlt(DirectLight, directColor, currWeight);
        IndirectLight = LerpPackedColorAlt(IndirectLight, bounceColor, currWeight);
        return false;
    }

    float DetectDistance(IRayTracer tracer, float3 origin, float3 rayDir)
    {
        Vertex hitVertex;
        NodeProxy hitNode;

        uint TempInstanceId;
        if (tracer.TraceRay(origin, rayDir, CUBE_UNIT * 64, hitVertex, hitNode))
        {
            return length(hitVertex.Position - origin);
        }
        return 255;
    }

    bool InsideGeometry(IRayTracer tracer, inout float3 origin, float3 rayDir, inout uint OutMaterialId, out float OutDistanceToSolid)
    {
        // 求交测试
        Vertex hitVertex;
        NodeProxy hitNode;

        if (tracer.TraceRay(origin, rayDir, CUBE_UNIT * 64, hitVertex, hitNode))
        {
            float hitDist = length(hitVertex.Position - origin);
            OutDistanceToSolid = hitDist;

            if (OutDistanceToSolid <= CUBE_UNIT)
            {
                Material hitMaterial = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
                OutMaterialId = hitVertex.MaterialIndex;

                if (dot(hitVertex.Normal, rayDir) > 0.0 || ((hitMaterial.MaterialModel == MaterialDiffuseLight) && hitDist < 0.02f)) //voxel inclusive
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

        Voxels[CubeIdx].matId = 0;

        float distPY = 255.0f;
        float distNY = 255.0f;
        float distPX = 255.0f;
        float distNX = 255.0f;
        float distPZ = 255.0f;
        float distNZ = 255.0f;

        InsideGeometry(tracer, origin, float3(0, 1, 0), Voxels[CubeIdx].matId, distPY);
        InsideGeometry(tracer, origin, float3(0, -1, 0), Voxels[CubeIdx].matId, distNY);
        InsideGeometry(tracer, origin, float3(1, 0, 0), Voxels[CubeIdx].matId, distPX);
        InsideGeometry(tracer, origin, float3(-1, 0, 0), Voxels[CubeIdx].matId, distNX);
        InsideGeometry(tracer, origin, float3(0, 0, 1), Voxels[CubeIdx].matId, distPZ);
        InsideGeometry(tracer, origin, float3(0, 0, -1), Voxels[CubeIdx].matId, distNZ);

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
        Voxels[CubeIdx].distanceToSolid_gg_z01 = pack_bytes(uint4(uint(minDist / CUBE_UNIT), uint(inside * 255.0f), uint(distPZ * 255.0f), uint(distNZ * 255.0f)));
        Voxels[CubeIdx].distanceToSolid_x01_y01 = pack_bytes(uint4(uint(distPX * 255.0f), uint(distNX * 255.0f), uint(distPY * 255.0f), uint(distNY * 255.0f)));

        if (minDist < 4) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            uint iterate = Voxels[CubeIdx].age;
            Voxels[CubeIdx].age = Voxels[CubeIdx].age + 1;
            
            uint4 skyvis_pznzpynyx = unpack_bytes(Cubes[CubeIdx].skyVisibility_pznzpyny);
            uint4 skyvis_pxnxs0s1 = unpack_bytes(Cubes[CubeIdx].skyVisibility_pxnxs0s1);

            uint sunvis = 0;

            FaceTask(tracer, origin, float3(0, 1, 0), iterate, Cubes[CubeIdx].PosY_D, Cubes[CubeIdx].PosY, skyvis_pznzpynyx.z, skyvis_pxnxs0s1.z);
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, Cubes[CubeIdx].NegY_D, Cubes[CubeIdx].NegY, skyvis_pznzpynyx.w, sunvis);
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, Cubes[CubeIdx].PosX_D, Cubes[CubeIdx].PosX, skyvis_pxnxs0s1.x, sunvis);
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, Cubes[CubeIdx].NegX_D, Cubes[CubeIdx].NegX, skyvis_pxnxs0s1.y, sunvis);
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, Cubes[CubeIdx].PosZ_D, Cubes[CubeIdx].PosZ, skyvis_pznzpynyx.x, sunvis);
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, Cubes[CubeIdx].NegZ_D, Cubes[CubeIdx].NegZ, skyvis_pznzpynyx.y, sunvis);

            Cubes[CubeIdx].skyVisibility_pznzpyny = pack_bytes(skyvis_pznzpynyx);
            Cubes[CubeIdx].skyVisibility_pxnxs0s1 = pack_bytes(skyvis_pxnxs0s1);
        }
    }

    public void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, uint CubeIdx, inout uint4 RandomSeed)
    {
        AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
        VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

        uint4 upacked = unpack_bytes(Voxels[CubeIdx].distanceToSolid_gg_z01);
        if (upacked.x < 4) // 只处理表面，增值算法的时候可以具体的把他做好
        {
            uint iterate = Voxels[CubeIdx].age;
            Voxels[CubeIdx].age = Voxels[CubeIdx].age + 1;

            uint4 skyvis_pznzpynyx = unpack_bytes(Cubes[CubeIdx].skyVisibility_pznzpyny);
            uint4 skyvis_pxnxs0s1 = unpack_bytes(Cubes[CubeIdx].skyVisibility_pxnxs0s1);

            uint sunvis = 0;

            FaceTask(tracer, origin, float3(0, 1, 0), iterate, Cubes[CubeIdx].PosY_D, Cubes[CubeIdx].PosY, skyvis_pznzpynyx.z, skyvis_pxnxs0s1.z);
            FaceTask(tracer, origin, float3(0, -1, 0), iterate, Cubes[CubeIdx].NegY_D, Cubes[CubeIdx].NegY, skyvis_pznzpynyx.w, sunvis);
            FaceTask(tracer, origin, float3(1, 0, 0), iterate, Cubes[CubeIdx].PosX_D, Cubes[CubeIdx].PosX, skyvis_pxnxs0s1.x, sunvis);
            FaceTask(tracer, origin, float3(-1, 0, 0), iterate, Cubes[CubeIdx].NegX_D, Cubes[CubeIdx].NegX, skyvis_pxnxs0s1.y, sunvis);
            FaceTask(tracer, origin, float3(0, 0, 1), iterate, Cubes[CubeIdx].PosZ_D, Cubes[CubeIdx].PosZ, skyvis_pznzpynyx.x, sunvis);
            FaceTask(tracer, origin, float3(0, 0, -1), iterate, Cubes[CubeIdx].NegZ_D, Cubes[CubeIdx].NegZ, skyvis_pznzpynyx.y, sunvis);

            Cubes[CubeIdx].skyVisibility_pznzpyny = pack_bytes(skyvis_pznzpynyx);
            Cubes[CubeIdx].skyVisibility_pxnxs0s1 = pack_bytes(skyvis_pxnxs0s1);
        }
    }
}
