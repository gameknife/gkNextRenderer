// CPU BVH wrapper: tinybvh implementation unit, shared TLAS state and the
// CPU ray trace entry points.
// Split from CPUAccelerationStructure.cpp; same namespace, separate TU.
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include <atomic>

#define TINYBVH_IMPLEMENTATION
#include "ThirdParty/tinybvh/tiny_bvh.h"

namespace Assets::CPU
{

namespace
{
    std::atomic_bool GLoggedInvalidCpuAsHit{false};
    std::atomic_bool GLoggedInvalidCpuAsMaterial{false};
}

using namespace Assets;

uint FetchMaterialId(const FCPUTLASSnapshot& snapshot, uint materialIdx, uint instanceId)
{
    if (instanceId >= snapshot.contexts.size())
    {
        if (!GLoggedInvalidCpuAsMaterial.exchange(true))
        {
            SPDLOG_WARN("CPUAccelerationStructure: invalid TLAS instance {} while resolving material id.", instanceId);
        }
        return 0;
    }

    if (materialIdx >= snapshot.contexts[instanceId].matIdxs.size())
    {
        if (!GLoggedInvalidCpuAsMaterial.exchange(true))
        {
            SPDLOG_WARN(
                "CPUAccelerationStructure: section/material index {} exceeds supported material slots ({}); falling back to material 0.",
                materialIdx, snapshot.contexts[instanceId].matIdxs.size());
        }
        return 0;
    }

    return snapshot.contexts[instanceId].matIdxs[materialIdx];
}

bool TraceRay(const FCPUTLASSnapshot& snapshot, vec3 origin, vec3 rayDir, float dist, vec3& outNormal,
              uint& outMaterialId, float& outRayDist, uint& outInstanceId)
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), dist);
    snapshot.tlas.Intersect(ray);

    if (ray.hit.t < dist)
    {
        uint32_t primIdx = ray.hit.prim;
        if (!snapshot.blasSet || ray.hit.inst >= snapshot.instances.size() || ray.hit.inst >= snapshot.contexts.size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid hit instance {} during CPU ray trace.", ray.hit.inst);
            }
            return false;
        }

        const tinybvh::BLASInstance& instance = snapshot.instances[ray.hit.inst];
        const FCPUTLASInstanceInfo& instContext = snapshot.contexts[ray.hit.inst];
        if (instance.blasIdx >= snapshot.blasSet->contexts.size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid BLAS index {} during CPU ray trace.", instance.blasIdx);
            }
            return false;
        }

        const FCPUBLASContext& context = snapshot.blasSet->contexts[instance.blasIdx];
        if (primIdx >= context.extinfos.size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid primitive index {} during CPU ray trace.", primIdx);
            }
            return false;
        }

        mat4* worldTS = (mat4*)instance.transform;
        vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;

        outRayDist = ray.hit.t;
        outNormal = vec3(normalWS.x, normalWS.y, normalWS.z);
        outMaterialId = FetchMaterialId(snapshot, context.extinfos[primIdx].matIdx, ray.hit.inst);
        outInstanceId = instContext.nodeId;
        return true;
    }
    
    return false;
}
}
