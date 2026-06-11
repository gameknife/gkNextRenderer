// CPU BVH wrapper: tinybvh implementation unit, shared TLAS state and the
// CPU ray trace entry points.
// Split from CPUAccelerationStructure.cpp; same namespace, separate TU.
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.h"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include <atomic>

#define TINYBVH_IMPLEMENTATION
#include "ThirdParty/tinybvh/tiny_bvh.h"

namespace Assets::CPU
{

namespace
{
    FCpuBvhState GCpuBvhState;
    std::atomic_bool GLoggedInvalidCpuAsHit{false};
    std::atomic_bool GLoggedInvalidCpuAsMaterial{false};
}

FCpuBvhState& GetCpuBvhState()
{
    return GCpuBvhState;
}

using namespace Assets;

FMaterial& FetchMaterial(uint matId)
{
    auto& materials = NextEngine::GetInstance()->GetScene().Materials();
    assert(matId < materials.size());
    matId = matId % materials.size(); // wrap around
    return materials[matId];
}

uint FetchMaterialId(uint materialIdx, uint instanceId)
{
    if (GCpuBvhState.tlasContexts == nullptr || instanceId >= GCpuBvhState.tlasContexts->size())
    {
        if (!GLoggedInvalidCpuAsMaterial.exchange(true))
        {
            SPDLOG_WARN("CPUAccelerationStructure: invalid TLAS instance {} while resolving material id.", instanceId);
        }
        return 0;
    }

    if (materialIdx >= (*GCpuBvhState.tlasContexts)[instanceId].matIdxs.size())
    {
        if (!GLoggedInvalidCpuAsMaterial.exchange(true))
        {
            SPDLOG_WARN(
                "CPUAccelerationStructure: section/material index {} exceeds supported material slots ({}); falling back to material 0.",
                materialIdx, (*GCpuBvhState.tlasContexts)[instanceId].matIdxs.size());
        }
        return 0;
    }

    return (*GCpuBvhState.tlasContexts)[instanceId].matIdxs[materialIdx];
}

bool TraceRay(vec3 origin, vec3 rayDir, float dist, vec3& outNormal, uint& outMaterialId, float& outRayDist, uint& outInstanceId )
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), dist);
    GCpuBvhState.bvh.Intersect(ray);

    if (ray.hit.t < dist)
    {
        uint32_t primIdx = ray.hit.prim;
        if (GCpuBvhState.instanceList == nullptr || GCpuBvhState.tlasContexts == nullptr || GCpuBvhState.blasContexts == nullptr ||
            ray.hit.inst >= GCpuBvhState.instanceList->size() || ray.hit.inst >= GCpuBvhState.tlasContexts->size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid hit instance {} during CPU ray trace.", ray.hit.inst);
            }
            return false;
        }

        tinybvh::BLASInstance& instance = (*GCpuBvhState.instanceList)[ray.hit.inst];
        FCPUTLASInstanceInfo& instContext = (*GCpuBvhState.tlasContexts)[ray.hit.inst];
        if (instance.blasIdx >= GCpuBvhState.blasContexts->size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid BLAS index {} during CPU ray trace.", instance.blasIdx);
            }
            return false;
        }

        FCPUBLASContext& context = (*GCpuBvhState.blasContexts)[instance.blasIdx];
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
        outMaterialId =  FetchMaterialId( context.extinfos[primIdx].matIdx, ray.hit.inst );
        outInstanceId = instContext.nodeId;
        return true;
    }
    
    return false;
}
}
