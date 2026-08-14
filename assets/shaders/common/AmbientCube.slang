implementing PathTracing;

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

// Cascade N's cell size is baseUnit * ratio^N. cascadeIndex is always below CUBE_CASCADE_MAX (4),
// so two selects replace pow() -- which the drivers lower to exp2/log2 and which used to run
// several times per DDA step through the helpers below.
public float GetAmbientCubeUnitAtCascade(uint cascadeIndex)
{
    float ratio = GetAmbientCubeCascadeRatio();
    float ratioSquared = ratio * ratio;
    return GetAmbientCubeUnit() * ((cascadeIndex & 1u) != 0u ? ratio : 1.0f)
                                * ((cascadeIndex & 2u) != 0u ? ratioSquared : 1.0f);
}

public float3 GetAmbientCubeOffsetAtCascade(uint cascadeIndex)
{
    float unit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    return float3(-CUBE_SIZE_XY / 2.0f, -1.375f, -CUBE_SIZE_XY / 2.0f) * unit + GetAmbientCubeOffsetBias();
}

// Cascade + grid parameters for a world position in one pass. Callers that need both used to run
// the search and then re-derive the unit and offset from scratch, re-reading the UBO each time.
public bool GetAmbientCubeGridAtPosition(float3 worldPos, out uint cascadeIndex, out float unit, out float3 offset)
{
    uint cascadeCount = GetAmbientCubeCascadeCount();
    float ratio = GetAmbientCubeCascadeRatio();
    float3 offsetBias = GetAmbientCubeOffsetBias();
    const float3 cascadeCorner = float3(-CUBE_SIZE_XY / 2.0f, -1.375f, -CUBE_SIZE_XY / 2.0f);

    unit = GetAmbientCubeUnit();
    for (cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        offset = cascadeCorner * unit + offsetBias;
        float3 voxelPos = (worldPos - offset) / unit;
        if (voxelPos.x >= 0 && voxelPos.y >= 0 && voxelPos.z >= 0 && voxelPos.x <= CUBE_SIZE_XY - 1 &&
            voxelPos.y <= CUBE_SIZE_Z - 1 && voxelPos.z <= CUBE_SIZE_XY - 1)
        {
            return true;
        }
        unit *= ratio;
    }

    cascadeIndex = cascadeCount - 1;
    unit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    offset = cascadeCorner * unit + offsetBias;
    return false;
}

public uint GetAmbientCubeCascadeAtPosition(float3 worldPos)
{
    uint cascadeIndex;
    float unit;
    float3 offset;
    GetAmbientCubeGridAtPosition(worldPos, cascadeIndex, unit, offset);
    return cascadeIndex;
}

public float GetAmbientCubeUnitAtPosition(float3 worldPos)
{
    uint cascadeIndex;
    float unit;
    float3 offset;
    GetAmbientCubeGridAtPosition(worldPos, cascadeIndex, unit, offset);
    return unit;
}

public uint GetCascadeBaseOffset(uint cascadeIndex)
{
    return cascadeIndex * uint(CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z);
}

public uint PackNibbles(uint4 low, uint4 high)
{
    return (low.x & 0xFu) |
           ((low.y & 0xFu) << 4) |
           ((low.z & 0xFu) << 8) |
           ((low.w & 0xFu) << 12) |
           ((high.x & 0xFu) << 16) |
           ((high.y & 0xFu) << 20) |
           ((high.z & 0xFu) << 24) |
           ((high.w & 0xFu) << 28);
}

public uint4 UnpackNibblesLow(uint packed)
{
    return uint4(
        packed & 0xFu,
        (packed >> 4) & 0xFu,
        (packed >> 8) & 0xFu,
        (packed >> 12) & 0xFu);
}

public uint4 UnpackNibblesHigh(uint packed)
{
    return uint4(
        (packed >> 16) & 0xFu,
        (packed >> 20) & 0xFu,
        (packed >> 24) & 0xFu,
        (packed >> 28) & 0xFu);
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

float AmbientCubeSkyVisibility(AmbientCube cube, float3 normal)
{
    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);
    float sum = wx + wnx + wy + wny + wz + wnz;

    uint4 unpacked0 = UnpackBytes(cube.SkyVisibilityPZNZPYNY);
    uint4 unpacked1 = UnpackBytes(cube.SkyVisibilityPXNXS0S1);

    float skyvis = 0.0;
    skyvis += float(unpacked0.x) * wz;
    skyvis += float(unpacked0.y) * wnz;
    skyvis += float(unpacked0.z) * wy;
    skyvis += float(unpacked0.w) * wny;
    skyvis += float(unpacked1.x) * wx;
    skyvis += float(unpacked1.y) * wnx;
    skyvis *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return skyvis / 255.0f;
}

float4 sampleAmbientCubeDirectBounce(AmbientCube cube, float3 normal)
{
    float4 color = UnpackColor(cube.SunDirect) + UnpackColor(cube.SkyEmissiveDirect);
    color.a = AmbientCubeSkyVisibility(cube, normal);
    return color;
}

float4 sampleAmbientCubeDirectSurface(AmbientCube cube, float3 normal)
{
    float4 color = UnpackColor(cube.SkyEmissiveDirect);
    color.a = AmbientCubeSkyVisibility(cube, normal);
    return color;
}


float4 sampleAmbientCubeHL2_Full(AmbientCube cube, float3 normal)
{
    float4 color = sampleAmbientCubeHL2_ID(cube, normal) + sampleAmbientCubeDirectSurface(cube, normal);
    color.a = AmbientCubeSkyVisibility(cube, normal);
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

public struct FullAmbientCubeWNSunSampler : IAmbientCubeSampler
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
        return sampleAmbientCubeDirectBounce(cube, normal);
    }
};

public struct LightCacheAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeDirectSurface(cube, normal);
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
    c.SunDirect = 0; c.SkyEmissiveDirect = 0;
    c.SkyVisibilityPZNZPYNY = 0; c.SkyVisibilityPXNXS0S1 = 0;
    return c;
}

// Brick coordinates are only ever formed from in-range probe positions, so the divide and modulo
// are done in unsigned: signed / 8 and % 8 make the compiler emit the sign-correction dance
// instead of a shift and a mask, and this runs eight times per probe interpolation.
static const uint kAmbientBrickEdgeShift = 3; // GPU_SCENE_AMBIENT_BRICK_EDGE == 8
static const uint kAmbientBrickEdgeMask = uint(GPU_SCENE_AMBIENT_BRICK_EDGE) - 1u;

uint AmbientBrickLinear(int3 probePos)
{
    uint bx = uint(probePos.x) >> kAmbientBrickEdgeShift;
    uint by = uint(probePos.y) >> kAmbientBrickEdgeShift;
    uint bz = uint(probePos.z) >> kAmbientBrickEdgeShift;
    return by * uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z)
         + bz * uint(GPU_SCENE_AMBIENT_BRICKS_X) + bx;
}

uint AmbientBrickSlot(uint cascadeIndex, int3 probePos)
{
    uint* table = Bindless.GetGpuscene().AmbientBrickTable;
    return table[cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) + AmbientBrickLinear(probePos)];
}

uint AmbientBrickLocalOffset(int3 probePos)
{
    uint lx = uint(probePos.x) & kAmbientBrickEdgeMask;
    uint ly = uint(probePos.y) & kAmbientBrickEdgeMask;
    uint lz = uint(probePos.z) & kAmbientBrickEdgeMask;
    return ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE)
         + lz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lx;
}

// Element index of a probe inside one cascade's dense VoxelData array. The array is brick-swizzled
// rather than laid out as y * W * W + z * W + x: the DDA reads it one cell at a time, and linearly
// a single Y step landed ~2 MB away, so nearly every step missed cache. Tiled into 8^3 blocks an
// entire neighbourhood is one 4 KB run. Must match GetVoxelAddress() in
// Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp, which writes the array.
public uint VoxelStorageIndex(int3 probePos)
{
    return AmbientBrickLinear(probePos) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME)
         + AmbientBrickLocalOffset(probePos);
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

    // Only the last-hit frames drive eviction (FCPUBrickTable::UpdateData); the hit counters are
    // written nowhere else and read by nothing, so they are left alone rather than costing a
    // second atomic on an address the whole wave already contends for.
    AmbientBrickResidency* residency = Bindless.GetGpuscene().AmbientResidency;
    uint residencyIndex = cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) + brickLinear;
    if (bounceHit)
    {
        InterlockedMax(residency[residencyIndex].lastBounceHitFrame, camera.TotalFrames);
    }
    else
    {
        InterlockedMax(residency[residencyIndex].lastConsumerHitFrame, camera.TotalFrames);
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

    voxelIdx = GetCascadeBaseOffset(cascadeIndex) + VoxelStorageIndex(probePos);
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
    return Cubes[VoxelStorageIndex(probePos)];
}

VoxelData FetchVoxel(int3 probePos, in VoxelData* Cubes, uint baseOffset)
{
    return Cubes[baseOffset + VoxelStorageIndex(probePos)];
}

// One trilinear probe gather, shared by the plain and the "stable" (pong pool) entry points.
// The corner loop is ordered cheapest-first: the trilinear weight needs no memory at all, the
// 8-byte VoxelData decides visibility, and only a corner that survives both pays for the 40-byte
// AmbientCube fetch behind the sparse brick indirection.
float4 gatherAmbientCubes<T : IAmbientCubeSampler>(float3 inPos, float3 normal, bool useStablePool,
                                                   uint stableCascadeIndex)
{
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    uint cascadeCount = GetAmbientCubeCascadeCount();
    float ratio = GetAmbientCubeCascadeRatio();
    float3 offsetBias = GetAmbientCubeOffsetBias();
    const float3 cascadeCorner = float3(-CUBE_SIZE_XY / 2.0f, -1.375f, -CUBE_SIZE_XY / 2.0f);
    float cubeUnit = GetAmbientCubeUnit();

    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex, cubeUnit *= ratio)
    {
        float3 cubeOffset = cascadeCorner * cubeUnit + offsetBias;
        float3 nearpos = (inPos - cubeOffset) / cubeUnit;

        if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 || nearpos.x > CUBE_SIZE_XY - 2 || nearpos.y > CUBE_SIZE_Z - 2 ||
            nearpos.z > CUBE_SIZE_XY - 2)
        {
            continue;
        }

        uint baseOffset = GetCascadeBaseOffset(cascadeIndex);
        int3 baseIdx = int3(floor(nearpos));
        float3 frac = frac(nearpos);

        // The eight corners span a single cell and the residency dilation is 3 bricks wide, so one
        // mark covers them all. Marking per corner meant eight hashes and eight atomics contending
        // for what is almost always the same brick.
        MarkAmbientBrickHit(cascadeIndex, baseIdx, false);

        float totalWeight = 0.0;
        float4 result = float4(0.0, 0.0, 0.0, 0.0);

        for (int i = 0; i < 8; i++)
        {
            int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;
            if (weight <= 0.0f)
            {
                continue;
            }

            int3 probePos = baseIdx + offset;
            uint packedDistance = FetchVoxel(probePos, Voxels, baseOffset).distanceToSolid;

            // Nibble y is the "inside geometry" marker; a probe buried in a solid contributes nothing.
            if (((packedDistance >> 4) & 0xFu) == 0u)
            {
                continue;
            }

            // Leak test: is the probe visible from the sample point, given the probe's per-face
            // clearance? Only the face the offset direction points at can be the blocker on each
            // axis, so take |dir| once and select that nibble -- the six sqrt(max(+-dir, 0)) pairs
            // this replaces evaluated three terms that were always zero.
            float3 probeToLocal = frac - float3(offset);
            float lengthSquared = dot(probeToLocal, probeToLocal);
            float invLength = rsqrt(max(lengthSquared, 1e-12f));
            float distanceToProbe = lengthSquared * invLength;
            float3 axisWeight = sqrt(abs(probeToLocal) * invLength);
            float3 faceDistance = float3(
                float((packedDistance >> (probeToLocal.x >= 0.0f ? 16u : 20u)) & 0xFu),
                float((packedDistance >> (probeToLocal.y >= 0.0f ? 24u : 28u)) & 0xFu),
                float((packedDistance >> (probeToLocal.z >= 0.0f ? 8u : 12u)) & 0xFu)) * (1.0f / 15.0f);

            if (distanceToProbe > dot(axisWeight, faceDistance) + 0.05f)
            {
                continue;
            }

            AmbientCube cube;
            bool hasCube = (useStablePool && cascadeIndex == stableCascadeIndex)
                ? TryFetchCubePongPool(probePos, cascadeIndex, cube)
                : TryFetchCubePool(probePos, cascadeIndex, cube);
            if (!hasCube)
            {
                continue;
            }

            result += T.Sample(cube, normal) * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0)
        {
            return result / totalWeight;
        }
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

public float4 interpolateAmbientCubes<T : IAmbientCubeSampler>(float3 inPos, float3 normal)
{
    float4 residencyDebugColor;
    if (TryAmbientResidencyDebug(inPos, residencyDebugColor))
    {
        return residencyDebugColor;
    }
    return gatherAmbientCubes<T>(inPos, normal, false, 0u);
}

public float4 interpolateAmbientCubesStable<T : IAmbientCubeSampler>(float3 inPos, float3 normal, uint stableCascadeIndex)
{
    return gatherAmbientCubes<T>(inPos, normal, true, stableCascadeIndex);
}

// Solid test for the voxel containing `pos` (cascade-local grid coordinates). `clearanceVoxels`
// comes back as the L-infinity distance transform in cells, always >= 1: every cell whose index
// differs from this one by less than that on all three axes is guaranteed empty, which is what
// lets the DDA jump ahead. See FCPUProbeBaker::RebuildDistanceField for how the field is built.
public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float clearanceVoxels,
                      in VoxelData* Cubes, uint baseOffset, float cubeUnit)
{
    clearanceVoxels = 1;
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 2 || pos.y > CUBE_SIZE_Z - 2 || pos.z > CUBE_SIZE_XY - 2)
    {
        return false;
    }

    int3 baseIdx = int3(floor(pos));

    VoxelData cube = FetchVoxel(baseIdx, Cubes, baseOffset);
    matId = cube.matId;

    uint4 unpack0 = UnpackNibblesLow(cube.distanceToSolid);
    if (cube.matId > 0)
    {
        if (unpack0.y == 0)
        {
            offsetDist = 0;
            return true;
        }

        // The per-face nibbles only matter once the voxel is known to be occupied, and rayDir is
        // already unit length, so the axis weights are its components straight off.
        uint4 unpack1 = UnpackNibblesHigh(cube.distanceToSolid);
        float3 dir = -rayDir;

        float distPZ = unpack0.z / 15.0f;
        float distNZ = unpack0.w / 15.0f;
        float distPX = unpack1.x / 15.0f;
        float distNX = unpack1.y / 15.0f;
        float distPY = unpack1.z / 15.0f;
        float distNY = unpack1.w / 15.0f;

        float wx_pos = (max(dir.x, 0.0));
        float wx_neg = (max(-dir.x, 0.0));
        float wy_pos = (max(dir.y, 0.0));
        float wy_neg = (max(-dir.y, 0.0));
        float wz_pos = (max(dir.z, 0.0));
        float wz_neg = (max(-dir.z, 0.0));
        float hitLength = wx_pos * distPX + wx_neg * distNX +
                          wy_pos * distPY + wy_neg * distNY +
                          wz_pos * distPZ + wz_neg * distNZ;

        float hitLengthRef = abs(dir.x) + abs(dir.y) + abs(dir.z);

        if (hitLength < hitLengthRef)
        {
            offsetDist = hitLength * cubeUnit;
            return true;
        }
    }

    clearanceVoxels = max(1.0f, float(unpack0.x));
    return false;
}

public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float clearanceVoxels,
                      in VoxelData* Cubes)
{
    return inSolid(pos, rayDir, offsetDist, matId, clearanceVoxels, Cubes, 0, GetAmbientCubeUnit());
}

public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float clearanceVoxels,
                      in VoxelData* Cubes, uint baseOffset)
{
    return inSolid(pos, rayDir, offsetDist, matId, clearanceVoxels, Cubes, baseOffset, GetAmbientCubeUnit());
}

public interface IProbeGenerator
{
    void Render(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed);
    void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed);
}

// Bake-side logic (FGpuProbeGenerator) lives in AmbientCubeBaker.slang.
