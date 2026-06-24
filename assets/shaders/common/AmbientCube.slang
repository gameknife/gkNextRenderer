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

// VoxelData.age bit layout (no struct growth, see voxel-skyvisibility design §5.1):
//   bits [23..0]  : temporal counter (saturated to kTraceHistoryLength / mod 9 for jitter)
//   bits [31..24] : per-voxel sky visibility (0-255), baked by Bake.VoxelSkyVisibility
// All age mutations must go through these helpers so the cube baker's counter increment never
// bleeds into the sky-visibility high byte.
public uint GetVoxelAgeCounter(VoxelData v) { return v.age & 0x00FFFFFFu; }
public uint GetVoxelSkyVis(VoxelData v) { return (v.age >> 24) & 0xFFu; }

public void SetVoxelAgeCounter(inout VoxelData v, uint counter)
{
    v.age = (v.age & 0xFF000000u) | min(counter, 0x00FFFFFFu);
}

public void SetVoxelSkyVis(inout VoxelData v, uint skyVis)
{
    v.age = (v.age & 0x00FFFFFFu) | ((skyVis & 0xFFu) << 24);
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
    // Cube color faces carry no alpha; RGB9E5 gives wider HDR range + better
    // dark-tone precision at the same 32 bits (see ConstFunc.slang packRGB9E5).
    return packRGB9E5(source.rgb);
}

float4 UnpackColor(uint packed)
{
    return float4(unpackRGB9E5(packed), 0.0f);
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


float4 sampleAmbientCubeHL2_Full(AmbientCube cube, float3 normal, float sunIntensity)
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

    color += wx * UnpackColor(cube.PosX_D) * sunIntensity;
    color += wnx * UnpackColor(cube.NegX_D) * sunIntensity;
    color += wy * UnpackColor(cube.PosY_D) * sunIntensity;
    color += wny * UnpackColor(cube.NegY_D) * sunIntensity;
    color += wz * UnpackColor(cube.PosZ_D) * sunIntensity;
    color += wnz * UnpackColor(cube.NegZ_D) * sunIntensity;

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
        return sampleAmbientCubeHL2_Full(cube, normal, 1.0);
    }
};

public struct FullAmbientCubeWNSunSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_Full(cube, normal, 0.0);
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

static const float kTraceHistoryLength = 32.0f;

// ---------- Phase 3 sparse brick indirection ----------
// The cube grid is tiled into 8^3 bricks; the per-cascade brick table maps a brick to a pool slot
// (or INVALID when unallocated). probePos below is a cascade-local grid coordinate in [0,CUBE_SIZE).

public AmbientCube ZeroAmbientCube()
{
    AmbientCube c;
    c.PosZ = 0; c.NegZ = 0; c.PosY = 0; c.NegY = 0; c.PosX = 0; c.NegX = 0;
    c.PosZ_D = 0; c.NegZ_D = 0; c.PosY_D = 0; c.NegY_D = 0; c.PosX_D = 0; c.NegX_D = 0;
    c.skyVisibility_pznzpyny = 0; c.skyVisibility_pxnxs0s1 = 0;
    return c;
}

uint AmbientBrickSlot(uint cascadeIndex, int3 probePos)
{
    uint* table = Bindless.GetGpuscene().AmbientBrickTable;
    int bx = probePos.x / GPU_SCENE_AMBIENT_BRICK_EDGE;
    int by = probePos.y / GPU_SCENE_AMBIENT_BRICK_EDGE;
    int bz = probePos.z / GPU_SCENE_AMBIENT_BRICK_EDGE;
    uint brickLinear = uint(by) * uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z)
                     + uint(bz) * uint(GPU_SCENE_AMBIENT_BRICKS_X) + uint(bx);
    return table[cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) + brickLinear];
}

uint AmbientBrickLocalOffset(int3 probePos)
{
    int lx = probePos.x % GPU_SCENE_AMBIENT_BRICK_EDGE;
    int ly = probePos.y % GPU_SCENE_AMBIENT_BRICK_EDGE;
    int lz = probePos.z % GPU_SCENE_AMBIENT_BRICK_EDGE;
    return uint(ly) * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE)
         + uint(lz) * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + uint(lx);
}

uint AmbientBrickLinear(int3 probePos)
{
    uint bx = uint(probePos.x / GPU_SCENE_AMBIENT_BRICK_EDGE);
    uint by = uint(probePos.y / GPU_SCENE_AMBIENT_BRICK_EDGE);
    uint bz = uint(probePos.z / GPU_SCENE_AMBIENT_BRICK_EDGE);
    return by * uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z)
         + bz * uint(GPU_SCENE_AMBIENT_BRICKS_X) + bx;
}

uint AmbientResidencyHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

public void MarkAmbientBrickHit(uint cascadeIndex, int3 probePos, bool bounceHit)
{
    if (probePos.x < 0 || probePos.y < 0 || probePos.z < 0 ||
        probePos.x >= CUBE_SIZE_XY || probePos.y >= CUBE_SIZE_Z || probePos.z >= CUBE_SIZE_XY)
    {
        return;
    }

    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    float markRatio = saturate(camera.AmbientCubeCascadeParams.z);
    if (markRatio <= 0.0f)
    {
        return;
    }

    uint brickLinear = AmbientBrickLinear(probePos);
    uint hashValue = AmbientResidencyHash(
        brickLinear + cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) +
        camera.TotalFrames * 1664525u);
    if (float(hashValue & 0xFFFFu) / 65535.0f > markRatio)
    {
        return;
    }

    AmbientBrickResidency* residency = Bindless.GetGpuscene().AmbientResidency;
    uint residencyIndex = cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) + brickLinear;
    if (bounceHit)
    {
        InterlockedMax(residency[residencyIndex].lastBounceHitFrame, camera.TotalFrames);
        InterlockedAdd(residency[residencyIndex].bounceHitCount, 1);
    }
    else
    {
        InterlockedMax(residency[residencyIndex].lastConsumerHitFrame, camera.TotalFrames);
        InterlockedAdd(residency[residencyIndex].consumerHitCount, 1);
    }
}

public void MarkAmbientBrickHitAtWorld(float3 worldPos, bool bounceHit)
{
    uint cascadeIndex = GetAmbientCubeCascadeAtPosition(worldPos);
    float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    float3 cubeOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
    int3 probePos = int3(floor((worldPos - cubeOffset) / cubeUnit));
    MarkAmbientBrickHit(cascadeIndex, probePos, bounceHit);
}

public bool TryAmbientResidencyDebug(float3 worldPos, out float4 color)
{
    uint debugMode = uint(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.w);
    if (debugMode == 0u)
    {
        color = float4(0.0f);
        return false;
    }

    uint cascadeIndex = GetAmbientCubeCascadeAtPosition(worldPos);
    float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    float3 cubeOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
    int3 probePos = int3(floor((worldPos - cubeOffset) / cubeUnit));
    if (probePos.x < 0 || probePos.y < 0 || probePos.z < 0 ||
        probePos.x >= CUBE_SIZE_XY || probePos.y >= CUBE_SIZE_Z || probePos.z >= CUBE_SIZE_XY)
    {
        color = float4(180.0f, 0.0f, 180.0f, 1.0f);
        return true;
    }

    uint brickLinear = AmbientBrickLinear(probePos);
    uint residencyIndex = cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) + brickLinear;
    AmbientBrickResidency residency = Bindless.GetGpuscene().AmbientResidency[residencyIndex];
    if (debugMode == 1u)
    {
        uint lastHit = max(residency.lastConsumerHitFrame, residency.lastBounceHitFrame);
        if (lastHit == 0u)
        {
            color = float4(28.0f, 28.0f, 28.0f, 1.0f);
        }
        else
        {
            float age = saturate(float(Bindless.GetGpuscene().Camera[0].TotalFrames - lastHit) / 180.0f);
            color = float4(255.0f * age, 255.0f * (1.0f - age), 16.0f, 1.0f);
        }
        return true;
    }

    uint slot = Bindless.GetGpuscene().AmbientBrickTable[residencyIndex];
    color = slot == GPU_SCENE_AMBIENT_BRICK_INVALID
        ? float4(255.0f, 18.0f, 18.0f, 1.0f)
        : float4(18.0f, 90.0f, 255.0f, 1.0f);
    return true;
}

// Global cube-pool element index for a cascade-local probe, or -1 if its brick is unallocated.
public int AmbientCubePoolIndex(uint cascadeIndex, int3 probePos)
{
    uint slot = AmbientBrickSlot(cascadeIndex, probePos);
    if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID) return -1;
    uint poolBricks = Bindless.GetGpuscene().AmbientPoolBricksPerCascade;
    return int((cascadeIndex * poolBricks + slot) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME)
               + AmbientBrickLocalOffset(probePos));
}

// Pong pool index (single baking cascade, no cascade base) for a probe, or -1 if unallocated.
public int AmbientCubePongPoolIndex(uint cascadeIndex, int3 probePos)
{
    uint slot = AmbientBrickSlot(cascadeIndex, probePos);
    if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID) return -1;
    return int(slot * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME) + AmbientBrickLocalOffset(probePos));
}

public bool DecodeAmbientActiveBrickProbe(uint cascadeIndex,
                                          uint activeBrickIndex,
                                          uint localProbeOffset,
                                          out int3 probePos,
                                          out uint voxelIdx,
                                          out int cubeIdx)
{
    uint poolBricks = Bindless.GetGpuscene().AmbientPoolBricksPerCascade;
    uint packedBrick = Bindless.GetGpuscene().AmbientActiveBrickList[cascadeIndex * poolBricks + activeBrickIndex];
    if (packedBrick == GPU_SCENE_AMBIENT_BRICK_INVALID)
    {
        probePos = int3(0, 0, 0);
        voxelIdx = 0;
        cubeIdx = -1;
        return false;
    }

    uint bricksPerY = uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z);
    uint by = packedBrick / bricksPerY;
    uint bz = (packedBrick - by * bricksPerY) / uint(GPU_SCENE_AMBIENT_BRICKS_X);
    uint bx = packedBrick - by * bricksPerY - bz * uint(GPU_SCENE_AMBIENT_BRICKS_X);

    uint ly = localProbeOffset / uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE);
    uint lz = (localProbeOffset - ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE)) /
              uint(GPU_SCENE_AMBIENT_BRICK_EDGE);
    uint lx = localProbeOffset - ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE) -
              lz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE);

    uint x = bx * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lx;
    uint y = by * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + ly;
    uint z = bz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lz;
    probePos = int3(int(x), int(y), int(z));

    uint localVoxelIdx = y * CUBE_SIZE_XY * CUBE_SIZE_XY + z * CUBE_SIZE_XY + x;
    voxelIdx = GetCascadeBaseOffset(cascadeIndex) + localVoxelIdx;
    uint slot = AmbientBrickSlot(cascadeIndex, probePos);
    cubeIdx = int((cascadeIndex * poolBricks + slot) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME) +
                  localProbeOffset);
    return true;
}

AmbientCube FetchCubePool(int3 probePos, uint cascadeIndex)
{
    int idx = AmbientCubePoolIndex(cascadeIndex, probePos);
    if (idx < 0) return ZeroAmbientCube();
    return Bindless.GetGpuscene().Cubes[idx];
}

bool TryFetchCubePool(int3 probePos, uint cascadeIndex, out AmbientCube cube)
{
    int idx = AmbientCubePoolIndex(cascadeIndex, probePos);
    if (idx < 0)
    {
        cube = ZeroAmbientCube();
        return false;
    }
    cube = Bindless.GetGpuscene().Cubes[idx];
    return true;
}

bool TryFetchCubePongPool(int3 probePos, uint cascadeIndex, out AmbientCube cube)
{
    int idx = AmbientCubePongPoolIndex(cascadeIndex, probePos);
    if (idx < 0)
    {
        cube = ZeroAmbientCube();
        return false;
    }
    cube = Bindless.GetGpuscene().CubesPong[idx];
    return true;
}

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
    float4 residencyDebugColor;
    if (TryAmbientResidencyDebug(inPos, residencyDebugColor))
    {
        return residencyDebugColor;
    }

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
            MarkAmbientBrickHit(cascadeIndex, probePos, false);
            AmbientCube cube;
            if (!TryFetchCubePool(probePos, cascadeIndex, cube))
            {
                continue;
            }
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

// Trilinear sample of the per-voxel baked sky visibility (VoxelData.age high byte).
// Returns [0,1] where 1 = fully open to the sky. `normal` is the shading surface normal.
//
// Anti-leak is done with SMOOTH multiplicative weights, not hard accept/reject. Hard rejections
// (binary continue) make the 8-tap footprint flip discontinuously as the sample point moves, which
// reads as blocky "grid" artifacts; instead each probe's trilinear weight is scaled by a continuous
// visibility factor (directional distance field) and a continuous normal-side factor, so leaked
// probes fade out smoothly. The result stays a convex combination of probe values -> always smooth
// and bounded. A plain-trilinear fallback (no visibility weighting) covers silhouette edges so we
// never snap to 1.0 and punch a bright hole; 1.0 is returned only when no valid baked voxel exists.
public float SampleVoxelSkyVisibility(float3 worldPos, float3 normal)
{
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    uint cascadeCount = GetAmbientCubeCascadeCount();

    // Default when no cascade has any valid baked voxel (warmup / outside every cascade): fully
    // visible, so callers degrade gracefully to pure GTAO.
    float fallback = 1.0f;

    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
        float3 cubeOffset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
        float3 nearpos = (worldPos - cubeOffset) / cubeUnit;

        if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 || nearpos.x > CUBE_SIZE_XY - 2 ||
            nearpos.y > CUBE_SIZE_Z - 2 || nearpos.z > CUBE_SIZE_XY - 2)
        {
            continue;
        }

        uint baseOffset = GetCascadeBaseOffset(cascadeIndex);
        int3 baseIdx = int3(floor(nearpos));
        float3 f = frac(nearpos);

        float totalWeight = 0.0f;
        float result = 0.0f;
        float fbWeight = 0.0f;
        float fbResult = 0.0f;

        for (int i = 0; i < 8; i++)
        {
            int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
            int3 probePos = baseIdx + offset;
            VoxelData voxel = FetchVoxel(probePos, Voxels, baseOffset);

            uint4 unpacked0 = UnpackBytes(voxel.distanceToSolid_gg_z01);
            uint4 unpacked1 = UnpackBytes(voxel.distanceToSolid_x01_y01);

            float wx = offset.x == 0 ? (1.0f - f.x) : f.x;
            float wy = offset.y == 0 ? (1.0f - f.y) : f.y;
            float wz = offset.z == 0 ? (1.0f - f.z) : f.z;
            float weight = wx * wy * wz;

            // Solid / interior voxel (inside ~= 0): zero sky visibility. Count it as a known occluder
            // (skyv = 0, full weight) instead of skipping, so a tight gap whose footprint is all solid
            // resolves dark rather than falling back to fully-visible (the bright-cell artifact in
            // crevices). Open surfaces are unaffected: with the 1-voxel normal bias their footprint is
            // air, so no solid voxel enters the 8-tap.
            if (float(unpacked0.y) / 255.0f < 0.01f)
            {
                fbWeight += weight;     // skyv = 0
                totalWeight += weight;  // skyv = 0
                continue;
            }
            // Sky-vis byte 0 == not yet baked (the baker stores [1,255]); skip during warmup.
            uint skyVisByte = GetVoxelSkyVis(voxel);
            if (skyVisByte == 0u)
            {
                continue;
            }

            float skyv = float(skyVisByte) / 255.0f;

            // Plain-trilinear fallback (no visibility weighting) for silhouette edges.
            fbResult += skyv * weight;
            fbWeight += weight;

            // --- smooth visibility weighting (anti-leak, no hard cutoff) ---
            float3 probeToLocal = f - float3(offset);
            float distanceToProbe = length(probeToLocal);
            float3 dir = distanceToProbe > 1e-5f ? probeToLocal / distanceToProbe : float3(0, 1, 0);

            // 1) directional distance-field visibility: how much can the probe "see" toward the sample?
            float distPZ = float(unpacked0.z) / 255.0f;
            float distNZ = float(unpacked0.w) / 255.0f;
            float distPX = float(unpacked1.x) / 255.0f;
            float distNX = float(unpacked1.y) / 255.0f;
            float distPY = float(unpacked1.z) / 255.0f;
            float distNY = float(unpacked1.w) / 255.0f;
            float hitLength = sqrt(max(dir.x, 0.0f)) * distPX + sqrt(max(-dir.x, 0.0f)) * distNX +
                              sqrt(max(dir.y, 0.0f)) * distPY + sqrt(max(-dir.y, 0.0f)) * distNY +
                              sqrt(max(dir.z, 0.0f)) * distPZ + sqrt(max(-dir.z, 0.0f)) * distNZ;
            // 1 when the probe clearly reaches the sample, ramping to 0 over half a voxel once the
            // path is blocked (smooth instead of a binary reject).
            float visFactor = saturate((hitLength + 0.1f - distanceToProbe) / 0.5f);

            // 2) normal-side factor: probes behind the shading surface (other side of a thin wall)
            // fade out; co-planar / front probes keep full weight.
            float3 probeWorld = float3(probePos) * cubeUnit + cubeOffset;
            float3 toProbe = probeWorld - worldPos;
            float normalSide = saturate(dot(normalize(toProbe + normal * 1e-3f), normal) * 2.0f + 1.0f);

            float w = weight * visFactor * normalSide;
            result += skyv * w;
            totalWeight += w;
        }

        // Blend the visibility-weighted (anti-leak) result with the plain-trilinear average by a
        // smooth coverage confidence, instead of a hard switch. `conf` = how much of the trilinear
        // footprint survived the visibility weighting. Where coverage is full (open surfaces) we trust
        // the anti-leak result; where it collapses (concave edges / sparse pockets, where dividing by a
        // tiny weight would otherwise amplify a single dark probe into a blocky black patch) we ease
        // toward the smooth plain-trilinear average. This removes the hard black voxel blocks at edges
        // while keeping leak suppression on well-covered surfaces.
        if (fbWeight > 1e-5f)
        {
            float fbAvg = fbResult / fbWeight;
            float visAvg = totalWeight > 1e-5f ? result / totalWeight : fbAvg;
            float conf = smoothstep(0.05f, 0.6f, totalWeight / fbWeight);
            return lerp(fbAvg, visAvg, conf);
        }
    }

    return fallback;
}

public float4 interpolateAmbientCubesStable<T : IAmbientCubeSampler>(float3 inPos, float3 normal, uint stableCascadeIndex)
{
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
            MarkAmbientBrickHit(cascadeIndex, probePos, false);
            AmbientCube cube;
            bool hasCube = (cascadeIndex == stableCascadeIndex)
                ? TryFetchCubePongPool(probePos, cascadeIndex, cube)
                : TryFetchCubePool(probePos, cascadeIndex, cube);
            if (!hasCube)
            {
                continue;
            }
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
    void Render(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed);
    void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed);
}

// Bake-side logic (FGpuProbeGenerator) lives in AmbientCubeBaker.slang.
