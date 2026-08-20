#pragma once

// Private implementation details for the core CPU scene acceleration system.

// Internal declarations shared between the CPUAccelerationStructure partial
// TUs (CpuBvh / ProbeBaker / BrickPageTable / main). Not engine public API.

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"

namespace Assets::CPU
{
    constexpr uint32_t kCascadeVoxelCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
    // Dilate the solid-brick set by this many bricks when classifying active cube bricks, so the
    // near-surface shell the bake writes is fully covered.
    constexpr int kAmbientBrickDilationRadius = 3;
    constexpr uint8_t kMaxDistanceFieldSeed = 15;

    // Voxel storage is brick-swizzled: the cascade array is tiled into GPU_SCENE_AMBIENT_BRICK_EDGE^3
    // blocks rather than stored as y * W * W + z * W + x. The shader DDA walks this array one cell at
    // a time, and under the linear layout a single Y step moved the read ~2 MB away, so almost every
    // step missed cache. Tiled, a whole 8^3 neighbourhood is one contiguous 4 KB run.
    // VoxelStorageIndex() in assets/shaders/common/AmbientCube.slang must match this exactly.
    constexpr uint32_t kVoxelBrickEdge = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_EDGE);
    constexpr uint32_t kVoxelBrickVolume = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
    constexpr uint32_t kVoxelBricksX = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_X);
    constexpr uint32_t kVoxelBricksZ = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_Z);

    constexpr uint32_t GetVoxelAddress(int x, int y, int z)
    {
        const uint32_t ux = static_cast<uint32_t>(x);
        const uint32_t uy = static_cast<uint32_t>(y);
        const uint32_t uz = static_cast<uint32_t>(z);
        const uint32_t brickLinear = (uy / kVoxelBrickEdge) * (kVoxelBricksX * kVoxelBricksZ) +
                                     (uz / kVoxelBrickEdge) * kVoxelBricksX + (ux / kVoxelBrickEdge);
        const uint32_t localOffset = (uy % kVoxelBrickEdge) * (kVoxelBrickEdge * kVoxelBrickEdge) +
                                     (uz % kVoxelBrickEdge) * kVoxelBrickEdge + (ux % kVoxelBrickEdge);
        return brickLinear * kVoxelBrickVolume + localOffset;
    }

    // Inverse of GetVoxelAddress, for the passes that sweep the array and need the grid coordinate.
    constexpr void DecodeVoxelAddress(uint32_t address, uint32_t& x, uint32_t& y, uint32_t& z)
    {
        const uint32_t brickLinear = address / kVoxelBrickVolume;
        const uint32_t localOffset = address % kVoxelBrickVolume;
        const uint32_t by = brickLinear / (kVoxelBricksX * kVoxelBricksZ);
        const uint32_t bz = (brickLinear - by * (kVoxelBricksX * kVoxelBricksZ)) / kVoxelBricksX;
        const uint32_t bx = brickLinear - by * (kVoxelBricksX * kVoxelBricksZ) - bz * kVoxelBricksX;
        const uint32_t ly = localOffset / (kVoxelBrickEdge * kVoxelBrickEdge);
        const uint32_t lz = (localOffset - ly * (kVoxelBrickEdge * kVoxelBrickEdge)) / kVoxelBrickEdge;
        const uint32_t lx = localOffset - ly * (kVoxelBrickEdge * kVoxelBrickEdge) - lz * kVoxelBrickEdge;
        x = bx * kVoxelBrickEdge + lx;
        y = by * kVoxelBrickEdge + ly;
        z = bz * kVoxelBrickEdge + lz;
    }

    uint FetchMaterialId(const FCPUTLASSnapshot& snapshot, uint materialIdx, uint instanceId);
    bool TraceRay(const FCPUTLASSnapshot& snapshot, glm::vec3 origin, glm::vec3 rayDir, float dist,
                  glm::vec3& outNormal, uint& outMaterialId, float& outRayDist, uint& outInstanceId);
}
