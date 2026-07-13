#pragma once

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

    // Shared tinybvh TLAS state, owned by CpuBvh.cpp. The pointer members are
    // rebound by FCPUAccelerationStructure::UpdateBVH after each rebuild.
    struct FCpuBvhState
    {
        tinybvh::BVH bvh;
        std::vector<tinybvh::BLASInstance>* instanceList = nullptr;
        std::vector<FCPUTLASInstanceInfo>* tlasContexts = nullptr;
        std::vector<FCPUBLASContext>* blasContexts = nullptr;
    };
    FCpuBvhState& GetCpuBvhState();

    // CPU ray trace against the shared BVH state (CpuBvh.cpp)
    FMaterial& FetchMaterial(uint matId);
    uint FetchMaterialId(uint materialIdx, uint instanceId);
    bool TraceRay(glm::vec3 origin, glm::vec3 rayDir, float dist, glm::vec3& outNormal, uint& outMaterialId,
                  float& outRayDist, uint& outInstanceId);
}
