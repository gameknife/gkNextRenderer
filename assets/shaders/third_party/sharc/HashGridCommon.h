/*
 * Copyright (c) 2023-2026, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Hash grid overview
//
// HashGridCommon.h implements a reusable logarithmic world-space hash grid.
// SHaRC uses it to map hit positions, and optionally coarse normal direction,
// to stable cache entries, but the hash grid itself is independent of SHaRC and
// can be used as a general sparse spatial lookup structure.
//
// The grid chooses a voxel level from distance to the camera, then quantizes the
// world-space position at that level. The final key packs quantized XYZ,
// logarithmic level, and optional normal bits. The key is hashed to a base slot
// and resolved through a fixed-size linear probe bucket.
//
// Main operations:
// - HashGridInsertEntry(): compute key and atomically insert/find a slot.
// - HashGridFindEntry(): compute key and search for an existing slot.
// - HashGridGetLevel() / HashGridGetVoxelSize(): query the logarithmic LOD.
// - HashGridDebugColoredHash(), HashGridDebugOccupancy(), and
//   HashGridDebugHashCollisions(): visualize grid layout, occupancy, and
//   collision behavior.
//
// HASH_GRID_COMPACT selects 32-bit keys; otherwise 64-bit keys are used.
// If 64-bit atomics are unavailable, the grid can use a lock buffer for
// compare-exchange during insertion.

#ifndef HASH_GRID_PREFIX
#error HASH_GRID_PREFIX must be defined before including HashGridCommon.h
#endif

#ifndef HASH_GRID_CONST_PREFIX
#error HASH_GRID_CONST_PREFIX must be defined before including HashGridCommon.h
#endif

#define HASH_GRID_CONCAT(a, b) a##b
#define HASH_GRID_CONCAT2(a, b) HASH_GRID_CONCAT(a, b)
#define HashGrid_(name) HASH_GRID_CONCAT2(HASH_GRID_PREFIX, name)
#define HASH_GRID_(name) HASH_GRID_CONCAT2(HASH_GRID_CONST_PREFIX, _##name)

// Constant parameters
#if HASH_GRID_COMPACT
static const uint HASH_GRID_(KEY_BIT_NUM)           = 32; // 32-bit hash grid keys
static const uint HASH_GRID_(POSITION_BIT_NUM)      = 8;
static const uint HASH_GRID_(LEVEL_BIT_NUM)         = 5;
static const uint HASH_GRID_(NORMAL_BIT_NUM)        = 3;
typedef uint HashGrid_(Key);
#else // !HASH_GRID_COMPACT
static const uint HASH_GRID_(KEY_BIT_NUM)           = 64; // 64-bit hash grid keys. Reserve 1 bit for user data (e.g. responsive lighting signal in SHARC)
static const uint HASH_GRID_(POSITION_BIT_NUM)      = 17;
static const uint HASH_GRID_(LEVEL_BIT_NUM)         = 9;
static const uint HASH_GRID_(NORMAL_BIT_NUM)        = 3;
typedef uint64_t HashGrid_(Key);
#endif // HASH_GRID_COMPACT

static const uint HASH_GRID_(POSITION_BIT_MASK)     = (1u << HASH_GRID_(POSITION_BIT_NUM)) - 1;
static const uint HASH_GRID_(LEVEL_BIT_MASK)        = (1u << HASH_GRID_(LEVEL_BIT_NUM)) - 1;
static const uint HASH_GRID_(NORMAL_BIT_MASK)       = (1u << HASH_GRID_(NORMAL_BIT_NUM)) - 1;

static const uint HASH_GRID_(LEVEL_BIT_OFFSET)      = HASH_GRID_(POSITION_BIT_NUM) * 3;
static const uint HASH_GRID_(NORMAL_BIT_OFFSET)     = HASH_GRID_(LEVEL_BIT_OFFSET) + HASH_GRID_(LEVEL_BIT_NUM);

// Tweakable parameters
#ifndef HASH_GRID_ENABLE_64_BIT_ATOMICS
#define HASH_GRID_ENABLE_64_BIT_ATOMICS     1       // use 64-bit atomics for hash key insertion; if not available, a lock buffer will be used for synchronization
#endif

#ifndef HASH_GRID_USE_NORMALS
#define HASH_GRID_USE_NORMALS               1       // account for the normal data in the hash key
#endif

#ifndef HASH_GRID_POSITION_BIAS
#define HASH_GRID_POSITION_BIAS             1e-4f   // may require adjustment for extreme scene scales
#endif

#ifndef HASH_GRID_NORMAL_BIAS
#define HASH_GRID_NORMAL_BIAS               1e-3f   // bias for normal to stabilize hashing on surfaces nearly parallel to coordinate planes
#endif

#ifndef HASH_GRID_LOOP_ATTR
#define HASH_GRID_LOOP_ATTR                 [loop]  // loop attribute used to control shader compiler unrolling behavior
#endif

#ifndef HASH_GRID_LIMIT_EMPTY_SLOTS
#define HASH_GRID_LIMIT_EMPTY_SLOTS         0       // stop searching after N empty slots; 0 means never stop early (no-op); we typically avoid unrolling for these loops
#endif

// Computes logarithm with an arbitrary base.
// The base controls how frequently the hash grid level changes with distance
// (i.e., the rate at which voxel size increases across levels).
// SHaRC typically uses base 2.0 as a good default
float HashGrid_(LogBase)(float x, float base)
{
    return log(x) / log(base);
}

// http://burtleburtle.net/bob/hash/integer.html
uint HashGrid_(HashJenkins32)(uint a)
{
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);

    return a;
}

uint HashGrid_(Hash32)(HashGrid_(Key) hashKey)
{
#if HASH_GRID_COMPACT
    return HashGrid_(HashJenkins32)(hashKey);
#else // !HASH_GRID_COMPACT
    return HashGrid_(HashJenkins32)(uint((hashKey >> 0) & 0xFFFFFFFF)) ^ HashGrid_(HashJenkins32)(uint((hashKey >> 32) & 0xFFFFFFFF));
#endif // HASH_GRID_COMPACT
}

// Computes the first slot for the probe bucket.
// The bucket is kept contiguous and does not wrap around the end of the table,
// which simplifies lookup / insertion logic. Applications may allocate
// HASH_GRID_HASH_MAP_BUCKET_SIZE - 1 extra slots beyond capacity to avoid
// clamping the base slot near the end
uint HashGrid_(GetBaseSlot)(const HashGrid_(Key) hashKey, uint capacity)
{
    uint hash = HashGrid_(Hash32)(hashKey);
    uint slot = hash % capacity;

    return min(slot, capacity - HASH_GRID_HASH_MAP_BUCKET_SIZE);
}

uint HashGrid_(GetLevel)(float3 samplePosition, HashGridParameters gridParameters)
{
    float distance2 = dot(gridParameters.cameraPosition - samplePosition, gridParameters.cameraPosition - samplePosition);
    distance2 = max(distance2, 1e-10f);

    return uint(clamp(0.5f * HashGrid_(LogBase)(distance2, gridParameters.logarithmBase) + gridParameters.levelBias, 1.0f, float(HASH_GRID_(LEVEL_BIT_MASK))));
}

float HashGrid_(GetVoxelSize)(uint gridLevel, HashGridParameters gridParameters)
{
    return pow(gridParameters.logarithmBase, float(gridLevel)) / (gridParameters.sceneScale * pow(gridParameters.logarithmBase, gridParameters.levelBias));
}

// Based on logarithmic caching by Johannes Jendersie
int4 HashGrid_(CalculatePositionLog)(float3 samplePosition, HashGridParameters gridParameters)
{
    samplePosition += float3(HASH_GRID_POSITION_BIAS, HASH_GRID_POSITION_BIAS, HASH_GRID_POSITION_BIAS);

    uint gridLevel      = HashGrid_(GetLevel)(samplePosition, gridParameters);
    float voxelSize     = HashGrid_(GetVoxelSize)(gridLevel, gridParameters);
    int3 gridPosition   = int3(floor(samplePosition / voxelSize));

    return int4(gridPosition.xyz, gridLevel);
}

HashGrid_(Key) HashGrid_(ComputeSpatialHash)(float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters)
{
    uint4 gridPosition = uint4(HashGrid_(CalculatePositionLog)(samplePosition, gridParameters));

    HashGrid_(Key) hashKey =
        ((HashGrid_(Key)(gridPosition.x) & HASH_GRID_(POSITION_BIT_MASK)) << (HASH_GRID_(POSITION_BIT_NUM) * 0)) |
        ((HashGrid_(Key)(gridPosition.y) & HASH_GRID_(POSITION_BIT_MASK)) << (HASH_GRID_(POSITION_BIT_NUM) * 1)) |
        ((HashGrid_(Key)(gridPosition.z) & HASH_GRID_(POSITION_BIT_MASK)) << (HASH_GRID_(POSITION_BIT_NUM) * 2)) |
        ((HashGrid_(Key)(gridPosition.w) & HASH_GRID_(LEVEL_BIT_MASK)) << HASH_GRID_(LEVEL_BIT_OFFSET));

#if HASH_GRID_USE_NORMALS
    uint normalBits =
        (sampleNormal.x + HASH_GRID_NORMAL_BIAS >= 0 ? 0 : 1) +
        (sampleNormal.y + HASH_GRID_NORMAL_BIAS >= 0 ? 0 : 2) +
        (sampleNormal.z + HASH_GRID_NORMAL_BIAS >= 0 ? 0 : 4);

    hashKey |= (HashGrid_(Key)(normalBits) << HASH_GRID_(NORMAL_BIT_OFFSET));
#endif // HASH_GRID_USE_NORMALS

    return hashKey;
}

float3 HashGrid_(GetPositionFromKey)(const HashGrid_(Key) hashKey, HashGridParameters gridParameters)
{
    const uint signBit      = 1u << (HASH_GRID_(POSITION_BIT_NUM) - 1);
    const uint signMask     = ~((1u << HASH_GRID_(POSITION_BIT_NUM)) - 1);

    int3 gridPosition;
    gridPosition.x = int((hashKey >> (HASH_GRID_(POSITION_BIT_NUM) * 0)) & HASH_GRID_(POSITION_BIT_MASK));
    gridPosition.y = int((hashKey >> (HASH_GRID_(POSITION_BIT_NUM) * 1)) & HASH_GRID_(POSITION_BIT_MASK));
    gridPosition.z = int((hashKey >> (HASH_GRID_(POSITION_BIT_NUM) * 2)) & HASH_GRID_(POSITION_BIT_MASK));

    // Fix negative coordinates
    gridPosition.x = (gridPosition.x & signBit) != 0 ? gridPosition.x | signMask : gridPosition.x;
    gridPosition.y = (gridPosition.y & signBit) != 0 ? gridPosition.y | signMask : gridPosition.y;
    gridPosition.z = (gridPosition.z & signBit) != 0 ? gridPosition.z | signMask : gridPosition.z;

    uint   gridLevel        = uint((hashKey >> HASH_GRID_(LEVEL_BIT_OFFSET)) & HASH_GRID_(LEVEL_BIT_MASK));
    float  voxelSize        = HashGrid_(GetVoxelSize)(gridLevel, gridParameters);
    float3 samplePosition   = (gridPosition + 0.5f) * voxelSize;

    return samplePosition;
}

struct HashGrid_(Data)
{
    uint capacity;

    RW_STRUCTURED_BUFFER(hashEntriesBuffer, HashGrid_(Key));

#if !HASH_GRID_ENABLE_64_BIT_ATOMICS && !HASH_GRID_COMPACT
    RW_STRUCTURED_BUFFER(lockBuffer, uint);
#endif // !HASH_GRID_ENABLE_64_BIT_ATOMICS && !HASH_GRID_COMPACT
};

void HashGrid_(AtomicCompareExchange)(in HashGrid_(Data) hashData, in uint dstOffset, in HashGrid_(Key) compareValue, in HashGrid_(Key) value, out HashGrid_(Key) originalValue)
{
#if HASH_GRID_ENABLE_64_BIT_ATOMICS || HASH_GRID_COMPACT
#if SHARC_ENABLE_GLSL
    originalValue = InterlockedCompareExchange(BUFFER_AT_OFFSET(hashData.hashEntriesBuffer, dstOffset), compareValue, value);
#else // !SHARC_ENABLE_GLSL
    InterlockedCompareExchange(BUFFER_AT_OFFSET(hashData.hashEntriesBuffer, dstOffset), compareValue, value, originalValue);
#endif // SHARC_ENABLE_GLSL
#else // !HASH_GRID_ENABLE_64_BIT_ATOMICS
    // ANY rearangments to the code below lead to device hang if fuse is unlimited
    const uint cLock = 0xAAAAAAAA;
    uint fuse = 0;
    const uint fuseLength = 8;
    bool busy = true;
    while (busy && fuse < fuseLength)
    {
        uint state;
        InterlockedExchange(hashData.lockBuffer[dstOffset], cLock, state);
        busy = state != 0;

        if (state != cLock)
        {
            originalValue = BUFFER_AT_OFFSET(hashData.hashEntriesBuffer, dstOffset);
            if (originalValue == compareValue)
                BUFFER_AT_OFFSET(hashData.hashEntriesBuffer, dstOffset) = value;
            InterlockedExchange(hashData.lockBuffer[dstOffset], state, fuse);
            fuse = fuseLength;
        }
        ++fuse;
    }
#endif // HASH_GRID_ENABLE_64_BIT_ATOMICS
}

bool HashGrid_(Insert)(in HashGrid_(Data) hashData, const HashGrid_(Key) hashKey, uint baseSlot, uint probeRange, inout HashGridIndex cacheIndex, out uint bucketOffset)
{
    probeRange = min(probeRange, HASH_GRID_HASH_MAP_BUCKET_SIZE);

    HASH_GRID_LOOP_ATTR
    for (bucketOffset = 0; bucketOffset < probeRange; ++bucketOffset)
    {
        HashGrid_(Key) prevHashKey;
        HashGrid_(AtomicCompareExchange)(hashData, baseSlot + bucketOffset, HASH_GRID_INVALID_HASH_KEY, hashKey, prevHashKey);

        if (prevHashKey == HASH_GRID_INVALID_HASH_KEY || prevHashKey == hashKey)
        {
            cacheIndex = baseSlot + bucketOffset;
            return true;
        }
    }

    return false;
}

bool HashGrid_(Find)(in HashGrid_(Data) hashData, const HashGrid_(Key) hashKey, uint baseSlot, uint probeRange, inout HashGridIndex cacheIndex, out uint bucketOffset)
{
    probeRange = min(probeRange, HASH_GRID_HASH_MAP_BUCKET_SIZE);
    uint emptyEntires = 0;

    HASH_GRID_LOOP_ATTR
    for (bucketOffset = 0; bucketOffset < probeRange; ++bucketOffset)
    {
        HashGrid_(Key) storedHashKey = BUFFER_AT_OFFSET(hashData.hashEntriesBuffer, baseSlot + bucketOffset);
#if HASH_GRID_LIMIT_EMPTY_SLOTS
        if (storedHashKey == HASH_GRID_INVALID_HASH_KEY)
        {
            if (emptyEntires > HASH_GRID_LIMIT_EMPTY_SLOTS)
                break;

            ++emptyEntires;
        }
#endif // HASH_GRID_LIMIT_EMPTY_SLOTS

        if (storedHashKey == hashKey)
        {
            cacheIndex = baseSlot + bucketOffset;
            return true;
        }
    }

    return false;
}

HashGridIndex HashGrid_(InsertEntry)(in HashGrid_(Data) hashData, float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters, out HashGrid_(Key) hashKey)
{
    HashGridIndex cacheIndex    = HASH_GRID_INVALID_CACHE_INDEX;
    hashKey                     = HashGrid_(ComputeSpatialHash)(samplePosition, sampleNormal, gridParameters);
    uint baseSlot               = HashGrid_(GetBaseSlot)(hashKey, hashData.capacity);
    uint bucketOffset;
    bool successful             = HashGrid_(Insert)(hashData, hashKey, baseSlot, HASH_GRID_HASH_MAP_BUCKET_SIZE, cacheIndex, bucketOffset);

    return cacheIndex;
}

HashGridIndex HashGrid_(FindEntry)(in HashGrid_(Data) hashData, float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters, out HashGrid_(Key) hashKey)
{
    HashGridIndex cacheIndex    = HASH_GRID_INVALID_CACHE_INDEX;
    hashKey                     = HashGrid_(ComputeSpatialHash)(samplePosition, sampleNormal, gridParameters);
    uint baseSlot               = HashGrid_(GetBaseSlot)(hashKey, hashData.capacity);
    uint bucketOffset;
    bool successful             = HashGrid_(Find)(hashData, hashKey, baseSlot, HASH_GRID_HASH_MAP_BUCKET_SIZE, cacheIndex, bucketOffset);

    return cacheIndex;
}

// Debug functions
float3 HashGrid_(GetColorFromHash32)(uint hash)
{
    float3 color;
    color.x = ((hash >>  0) & 0x3ff) / 1023.0f;
    color.y = ((hash >> 11) & 0x7ff) / 2047.0f;
    color.z = ((hash >> 22) & 0x7ff) / 2047.0f;

    return color;
}

// Debug visualization
float3 HashGrid_(DebugColoredHash)(float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters)
{
    HashGrid_(Key) hashKey  = HashGrid_(ComputeSpatialHash)(samplePosition, sampleNormal, gridParameters);
    uint gridLevel          = HashGrid_(GetLevel)(samplePosition, gridParameters);
    float3 color            = HashGrid_(GetColorFromHash32)(HashGrid_(Hash32)(hashKey)) * HashGrid_(GetColorFromHash32)(HashGrid_(HashJenkins32)(gridLevel)).xyz;

    return color;
}

float3 HashGrid_(DebugOccupancy)(uint2 pixelPosition, uint2 screenSize, HashGrid_(Data) hashData)
{
    const uint elementSize = 7;
    const uint borderSize = 1;
    const uint blockSize = elementSize + borderSize;

    uint rowNum = screenSize.y / blockSize;
    uint rowIndex = pixelPosition.y / blockSize;
    uint columnIndex = pixelPosition.x / blockSize;
    uint elementIndex = (columnIndex / HASH_GRID_HASH_MAP_BUCKET_SIZE) * (rowNum * HASH_GRID_HASH_MAP_BUCKET_SIZE) + rowIndex * HASH_GRID_HASH_MAP_BUCKET_SIZE + (columnIndex % HASH_GRID_HASH_MAP_BUCKET_SIZE);

    if (elementIndex < hashData.capacity && ((pixelPosition.x % blockSize) < elementSize && (pixelPosition.y % blockSize) < elementSize))
    {
        HashGrid_(Key) hashGridKey = BUFFER_AT_OFFSET(hashData.hashEntriesBuffer, elementIndex);
        if (hashGridKey != HASH_GRID_INVALID_HASH_KEY)
            return (hashGridKey >> (HASH_GRID_KEY_BIT_NUM - 1)) != 0 ? float3(1.0f, 0.75f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
    }

    return float3(0.0f, 0.0f, 0.0f);
}

float3 HashGrid_(DebugHashCollisions)(float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters, HashGrid_(Data) hashData)
{
    HashGrid_(Key) hashKey      = HashGrid_(ComputeSpatialHash)(samplePosition, sampleNormal, gridParameters);
    HashGridIndex cacheIndex    = HASH_GRID_INVALID_CACHE_INDEX;
    uint baseSlot               = HashGrid_(GetBaseSlot)(hashKey, hashData.capacity);
    uint bucketOffset;
    HashGrid_(Find)(hashData, hashKey, baseSlot, HASH_GRID_HASH_MAP_BUCKET_SIZE, cacheIndex, bucketOffset);

    float3 debugColor;
    if (bucketOffset == 0)
        debugColor = float3(0.0f, 0.0f, 1.0f);
    else if (bucketOffset == 1)
        debugColor = float3(0.0f, 0.5f, 0.5f);
    else if (bucketOffset == 2)
        debugColor = float3(0.0f, 1.0f, 0.0f);
    else if (bucketOffset == 3)
        debugColor = float3(1.0f, 1.0f, 0.0f);
    else if (bucketOffset == 4)
        debugColor = float3(0.75f, 0.25f, 0.0f);
    else
        debugColor = float3(1.0f, 0.0f, 0.0f);

    return debugColor;
}
