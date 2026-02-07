#include "Assets/Acceleration/CPUAccelerationStructure.h"
#include "Runtime/Subsystems/TaskCoordinator.hpp"
#include "Vulkan/MemoryAndShader.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Assets/GPU/TextureImage.hpp"
#include "Runtime/Engine.hpp"
#include "Assets/Core/Scene.hpp"

#include <chrono>
#include <xxhash.h>

#define TINYBVH_IMPLEMENTATION
#include "ThirdParty/tinybvh/tiny_bvh.h"


static tinybvh::BVH GCpuBvh;
static std::vector<tinybvh::BLASInstance>* GbvhInstanceList;
static std::vector<FCPUTLASInstanceInfo>* GbvhTlasContexts;
static std::vector<FCPUBLASContext>* GbvhBlasContexts;

Assets::SphericalHarmonics HdrsHs[100];

namespace
{
    constexpr uint32_t kCascadeVoxelCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
}

using namespace Assets;

uint PackBytes(glm::u32vec4 values)
{
    return (values.x & 0xFF) |
           ((values.y & 0xFF) << 8) |
           ((values.z & 0xFF) << 16) |
           ((values.w & 0xFF) << 24);
}

FMaterial& FetchMaterial(uint matId)
{
    auto& materials = NextEngine::GetInstance()->GetScene().Materials();
    assert(matId < materials.size());
    matId = matId % materials.size(); // wrap around
    return materials[matId];
}

uint FetchMaterialId(uint materialIdx, uint instanceId)
{
    return (*GbvhTlasContexts)[instanceId].matIdxs[materialIdx];
}

bool TraceRay(vec3 origin, vec3 rayDir, float dist, vec3& outNormal, uint& outMaterialId, float& outRayDist, uint& outInstanceId )
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), dist);
    GCpuBvh.Intersect(ray);

    if (ray.hit.t < dist)
    {
        uint32_t primIdx = ray.hit.prim;
        tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
        FCPUTLASInstanceInfo& instContext = (*GbvhTlasContexts)[ray.hit.inst];
        FCPUBLASContext& context = (*GbvhBlasContexts)[instance.blasIdx];
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

#define FLOAT2 vec2
#define FLOAT3 vec3
#define FLOAT4 vec4

float DetectDistance(FLOAT3 origin, FLOAT3 rayDir, float cubeUnit)
{
    vec3 outNormal;
    float outRayDist;
    uint tempMaterialId;
    uint tempInstanceId;
    if (TraceRay(origin, rayDir, cubeUnit * 64.0f, outNormal, tempMaterialId, outRayDist, tempInstanceId))
    {
        return outRayDist;
    }
    return 255;
}

bool InsideGeometry(FLOAT3& origin, FLOAT3 rayDir, VoxelData& outCube, float& distance, float cubeUnit)
{
    // 求交测试
    vec3 outNormal;
    float outRayDist;
    uint tempMaterialId;
    uint tempInstanceId;

    if (TraceRay(origin, rayDir, cubeUnit * 64.0f, outNormal, tempMaterialId, outRayDist, tempInstanceId))
    {
        distance = outRayDist;
        if (distance <= cubeUnit)
        {
            FMaterial hitMaterial = FetchMaterial(tempMaterialId);
            outCube.matId = tempMaterialId;

            // 命中反面，识别为固体，并将lightprobe推出体外
            if (dot(outNormal, rayDir) > 0.0 || ((hitMaterial.gpuMaterial_.MaterialModel == Material::Enum::DiffuseLight)))// && OutRayDist < 0.02f))
            {
                distance = 0;
                return true;
            }
        }
    }
    return false;
}

void VoxelizeCube(VoxelData& cube, FLOAT3 origin, float cubeUnit)
{
    // just write matid and solid status
    cube.age = 0;
    cube.matId = 0;

    float distPY = 255.0f;
    float distNY = 255.0f;
    float distPX = 255.0f;
    float distNX = 255.0f;
    float distPZ = 255.0f;
    float distNZ = 255.0f;

    // 现在是向轴向上发射了6根光线，记录下距离，并用于后续采样判断
    InsideGeometry(origin, FLOAT3(0, 1, 0), cube, distPY, cubeUnit);
    InsideGeometry(origin, FLOAT3(0, -1, 0), cube, distNY, cubeUnit);
    InsideGeometry(origin, FLOAT3(1, 0, 0), cube, distPX, cubeUnit);
    InsideGeometry(origin, FLOAT3(-1, 0, 0), cube, distNX, cubeUnit);
    InsideGeometry(origin, FLOAT3(0, 0, 1), cube, distPZ, cubeUnit);
    InsideGeometry(origin, FLOAT3(0, 0, -1), cube, distNZ, cubeUnit);

    // get the min dist of each direction
    float minDist = std::min({distPY, distNY, distPX, distNX, distPZ, distNZ});
    if (minDist > 254.0f)
    {
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(1, 1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, -1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(1, 1, -1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, -1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, -1, -1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, -1), cubeUnit)});
    }

    // 现在，相当于每一个体素，都有了一个距离场，通过判断这个，可以快速跳过？
    distPY = glm::fclamp(distPY / cubeUnit, 0.0f, 1.0f);
    distNY = glm::fclamp(distNY / cubeUnit, 0.0f, 1.0f);
    distPX = glm::fclamp(distPX / cubeUnit, 0.0f, 1.0f);
    distNX = glm::fclamp(distNX / cubeUnit, 0.0f, 1.0f);
    distPZ = glm::fclamp(distPZ / cubeUnit, 0.0f, 1.0f);
    distNZ = glm::fclamp(distNZ / cubeUnit, 0.0f, 1.0f);

    float inside = distPY * distNY * distPX * distNX * distPZ * distNZ;

    cube.distanceToSolid_gg_z01 =
        PackBytes(glm::u32vec4(minDist / cubeUnit, uint(inside * 255.0f), uint(distPZ * 255.0f), uint(distNZ * 255.0f)));
    cube.distanceToSolid_x01_y01 = PackBytes(glm::u32vec4(uint(distPX * 255.0f), uint(distNX * 255.0f), uint(distPY * 255.0f), uint(distNY * 255.0f)));
}

#undef float2
#undef float3
#undef float4

void FCPUProbeBaker::Init(uint32_t cascadeIdx, float unitSize, vec3 offset)
{
    cascadeIndex = cascadeIdx;
    UNIT_SIZE = unitSize;
    CUBE_OFFSET = offset;
    voxels.resize(kCascadeVoxelCount);
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory, uint32_t elementOffset)
{
    const size_t byteOffset = static_cast<size_t>(elementOffset) * sizeof(VoxelData);
    VoxelData* data = reinterpret_cast<VoxelData*>(voxelGpuMemory.Map(byteOffset, sizeof(VoxelData) * voxels.size()));
    std::memcpy(data, voxels.data(), voxels.size() * sizeof(VoxelData));
    voxelGpuMemory.Unmap();
}

bool FCPUAccelerationStructure::InitCascadeBakers(const UserSettings& settings)
{
    const float baseUnit = SanitizeAmbientCubeUnit(settings.AmbientCubeUnit);
    const vec3 cubeOffsetBias = vec3(settings.AmbientCubeOffsetX, settings.AmbientCubeOffsetY, settings.AmbientCubeOffsetZ);
    const uint32_t cascadeCount = SanitizeAmbientCubeCascadeCount(settings.AmbientCubeCascadeCount);
    const float cascadeRatio = SanitizeAmbientCubeCascadeRatio(settings.AmbientCubeCascadeRatio);

    bool needRebuild = cascadeBakers.size() != cascadeCount;
    if (!needRebuild)
    {
        for (uint32_t i = 0; i < cascadeCount; ++i)
        {
            const float unit = CalculateAmbientCubeCascadeUnit(baseUnit, cascadeRatio, i);
            const vec3 offset = CalculateAmbientCubeOffset(unit, cubeOffsetBias);
            const FCPUProbeBaker& baker = cascadeBakers[i];
            if (glm::abs(unit - baker.UNIT_SIZE) > 1e-6f || glm::length(offset - baker.CUBE_OFFSET) > 1e-6f)
            {
                needRebuild = true;
                break;
            }
        }
    }

    if (!needRebuild)
    {
        return false;
    }

    cascadeBakers.clear();
    cascadeBakers.resize(cascadeCount);
    for (uint32_t i = 0; i < cascadeCount; ++i)
    {
        const float unit = CalculateAmbientCubeCascadeUnit(baseUnit, cascadeRatio, i);
        const vec3 offset = CalculateAmbientCubeOffset(unit, cubeOffsetBias);
        cascadeBakers[i].Init(i, unit, offset);
    }

    cpuPageIndex.Init();
    return true;
}

void FCPUAccelerationStructure::InitBVH(Scene& scene)
{
    auto& hdr = GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
    std::memcpy(HdrsHs, hdr.data(), hdr.size() * sizeof(SphericalHarmonics));
    
    const auto timer = std::chrono::high_resolution_clock::now();

    bvhBLASList.clear();
    bvhBLASContexts.clear();

    bvhBLASContexts.resize(scene.Models().size());
    for ( size_t m = 0; m < scene.Models().size(); ++m )
    {
        const Model& model = scene.Models()[m];
        for (size_t i = 0; i < model.CPUIndices().size(); i += 3)
        {
            // Get the three vertices of the triangle
            const Vertex& v0 = model.CPUVertices()[model.CPUIndices()[i]];
            const Vertex& v1 = model.CPUVertices()[model.CPUIndices()[i + 1]];
            const Vertex& v2 = model.CPUVertices()[model.CPUIndices()[i + 2]];
            
            // Calculate face normal
            vec3 edge1 = vec3(v1.Position) - vec3(v0.Position);
            vec3 edge2 = vec3(v2.Position) - vec3(v1.Position);
            vec3 normal = normalize(cross(edge1, edge2));
            
            // Add triangle vertices to BVH
            bvhBLASContexts[m].triangles.push_back(tinybvh::bvhvec4(v0.Position.x, v0.Position.y, v0.Position.z, 0));
            bvhBLASContexts[m].triangles.push_back(tinybvh::bvhvec4(v1.Position.x, v1.Position.y, v1.Position.z, 0));
            bvhBLASContexts[m].triangles.push_back(tinybvh::bvhvec4(v2.Position.x, v2.Position.y, v2.Position.z, 0));

            // Store additional triangle information
            bvhBLASContexts[m].extinfos.push_back({normal, v0.MaterialIndex});
        }

        // here we can cache the blas to disk if its big enough
        if (bvhBLASContexts[m].triangles.size() > 16384 * 3)
        {
            XXH64_hash_t vhash = XXH64(bvhBLASContexts[m].triangles.data(), bvhBLASContexts[m].triangles.size() * sizeof(tinybvh::bvhvec4), 0);
            std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", vhash), "cpubvh");

            if (!std::filesystem::exists(cacheFileName))
            {
                bvhBLASContexts[m].bvh.Build( bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
                bvhBLASContexts[m].bvh.Save(cacheFileName.c_str());
            }
            else
            {
                bvhBLASContexts[m].bvh.Load(cacheFileName.c_str(), bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
            }
        }
        else
        {
            bvhBLASContexts[m].bvh.Build( bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
        }

        bvhBLASList.push_back( &bvhBLASContexts[m].bvh );
    }
    
    const UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    InitCascadeBakers(settings);

    UpdateBVH(scene);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    std::vector<tinybvh::BLASInstance> tmpbvhInstanceList;
    std::vector<FCPUTLASInstanceInfo> tmpbvhTLASContexts;

    for (auto& node : scene.Nodes())
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render) continue;
        uint32_t modelId = render->GetModelId();
        if (modelId == -1) continue;
        if (!render->GetVisible()) continue;
        if (!render->GetRayCastVisible()) continue;

        node->RecalcTransform(true);
        mat4 worldTS = node->WorldTransform();
        worldTS = transpose(worldTS);

        tinybvh::BLASInstance instance;
        instance.blasIdx = modelId;
        std::memcpy( (float*)instance.transform, &(worldTS[0]), sizeof(float) * 16);

        tmpbvhInstanceList.push_back(instance);
        FCPUTLASInstanceInfo info;
        info.nodeId = node->GetInstanceId();
        auto& mats = render->Materials();
        for ( int i = 0; i < mats.size(); ++i )
        {
            uint32_t matId = mats[i];
            FMaterial& mat = scene.Materials()[matId];
            info.matIdxs[i] = matId;
            
        }
        tmpbvhTLASContexts.push_back( info );
    }

    if (tmpbvhInstanceList.size() > 0)
    {
        GCpuBvh.Build( tmpbvhInstanceList.data(), static_cast<int>(tmpbvhInstanceList.size()), bvhBLASList.data(), static_cast<int>(bvhBLASList.size()) );
    }

    TaskCoordinator::GetInstance()->WaitForAllParralledTask();

    bvhInstanceList.swap(tmpbvhInstanceList);
    bvhTLASContexts.swap(tmpbvhTLASContexts);
    
    // rebind with new address
    GbvhInstanceList = &bvhInstanceList;
    GbvhTlasContexts = &bvhTLASContexts;
    GbvhBlasContexts = &bvhBLASContexts;
}

RayCastResult FCPUAccelerationStructure::RayCastInCPU(vec3 rayOrigin, vec3 rayDir)
{
    RayCastResult result {};

    if (GCpuBvh.blasCount > 0)
    {
        tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), 2000.0f);
        GCpuBvh.Intersect(ray);
    
        if (ray.hit.t < 2000.f)
        {
            vec3 hitPos = rayOrigin + rayDir * ray.hit.t;
            uint32_t primIdx = ray.hit.prim;
            tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
            FCPUTLASInstanceInfo& instContext = (*GbvhTlasContexts)[ray.hit.inst];
            FCPUBLASContext& context = (*GbvhBlasContexts)[instance.blasIdx];
            mat4* worldTS = (mat4*)instance.transform;
            vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;
            result.HitPoint = vec4(hitPos, 0);
            result.Normal = normalWS;
            result.Hitted = true;
            result.T = ray.hit.t;
            result.InstanceId = instContext.nodeId;
        }
    }

    return result;
}

void FCPUProbeBaker::ProcessCube(int x, int y, int z, ECubeProcType procType)
{
    vec3 probePos = vec3(x, y, z) * UNIT_SIZE + CUBE_OFFSET;
    uint32_t addressIdx = y * CUBE_SIZE_XY * CUBE_SIZE_XY + z * CUBE_SIZE_XY + x;
    VoxelData& voxel = voxels[addressIdx];
        
    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
        case ECubeProcType::ECPT_Fence:
            break;
        case ECubeProcType::ECPT_Voxelize:
            VoxelizeCube(voxel, probePos, UNIT_SIZE);
            break;
    }
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory)
{
    UploadGPU(voxelGpuMemory, 0);
}

bool FCPUAccelerationStructure::AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* voxelGpuMemory, bool incremental)
{
    if ( !TaskCoordinator::GetInstance()->IsAllParralledTaskComplete() )
    {
        return false;
    }
    // clean
    while (!needUpdateGroups.empty())
        needUpdateGroups.pop();
    lastBatchTasks.clear();

    const UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    if (InitCascadeBakers(settings))
    {
        incremental = false;
    }

    if (!incremental)
    {
        for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
        {
            FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
            baker.ClearAmbientCubes();
            baker.UploadGPU(*voxelGpuMemory, cascadeIndex * kCascadeVoxelCount);
        }
    }
    else
    {
        UpdateBVH(scene);
    }
    
    const int groupSize = 16;
    const int lengthX = CUBE_SIZE_XY / groupSize;
    const int lengthZ = CUBE_SIZE_XY / groupSize;

    // far probe gen
    // for (int x = 0; x < lengthX; x++)
    //     for (int z = 0; z < lengthZ; z++)
    //         needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Voxelize, EBakerType::EBT_FarProbe});
    
    // 2 pass near probe iterate
    for(int pass = 0; pass < 1; ++pass)
    {
        // shuffle
        std::vector<std::pair<int, int>> coordinates;
        for (int x = 0; x < lengthX; x++)
            for (int z = 0; z < lengthZ; z++)
                coordinates.push_back({x, z});
        
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(coordinates.begin(), coordinates.end(), g);

        // dispatch
        for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
        {
            for (const auto& [x, z] : coordinates)
            {
                needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe, cascadeIndex});
            }
            needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe, cascadeIndex});
        }
        // add fence
    }

    return true;
}

void FCPUAccelerationStructure::AsyncProcessGroup(int xInMeter, int zInMeter, Scene& scene, ECubeProcType procType,
                                                  EBakerType bakerType, uint32_t cascadeIndex)
{
    (void)bakerType;
    if (bvhInstanceList.empty())
    {
        return;
    }

    if (cascadeIndex >= cascadeBakers.size())
    {
        return;
    }
    
    int groupSize = 16; // 4 x 4 x 40 a group
    
    int actualX = xInMeter * groupSize;
    int actualZ = zInMeter * groupSize;

    std::vector<vec3> lightPos;
    std::vector<vec3> sunDir;
    for( auto& light : scene.Lights() )
    {
        lightPos.push_back(mix(light.p1, light.p3, 0.5f));
    }

    if (scene.HasSun())
    {
        sunDir.push_back( scene.GetSunDir() );
    }

    uint32_t taskId = TaskCoordinator::GetInstance()->AddParralledTask(
                [this, actualX, actualZ, groupSize, procType, cascadeIndex](ResTask& task)
            {
                FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
                for (int z = actualZ; z < actualZ + groupSize; z++)
                    for (int y = 0; y < CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                        {
                            baker.ProcessCube(x, y, z, procType);
                        }
            },
            [this](ResTask& task)
            {
                // flush here
                //bakerType == EBakerType::EBT_Probe ? probeBaker.UploadGPU(*GPUMemory) : farProbeBaker.UploadGPU(*FarGPUMemory);
                needFlush = true;
            });

    lastBatchTasks.push_back(taskId);
}
void FCPUAccelerationStructure::ClearAllTasks()
{
    // 等待所有并行任务完成
    TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    
    // 清空待更新队列
    while (!needUpdateGroups.empty())
    {
        needUpdateGroups.pop();
    }
    
    // 清空当前批次任务列表
    lastBatchTasks.clear();
    
    // 重置刷新标志
    needFlush = false;
}

void FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* gpuMemory, Vulkan::DeviceMemory* voxelGpuMemory, Vulkan::DeviceMemory* pageIndexMemory)
{
    if (needFlush)
    {
        // Upload to GPU, now entire range, optimize to partial upload later
        for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
        {
            cascadeBakers[cascadeIndex].UploadGPU(*voxelGpuMemory, cascadeIndex * kCascadeVoxelCount);
        }
        if (!cascadeBakers.empty())
        {
            cpuPageIndex.UpdateData(cascadeBakers);
        }
        cpuPageIndex.UploadGPU(*pageIndexMemory);
        needFlush = false;
    }

    if (!lastBatchTasks.empty())
    {
        if (TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks))
        {
            lastBatchTasks.clear();
        }
    }
    else
    {
        while (!needUpdateGroups.empty())
        {
            auto& group = needUpdateGroups.front();
            ECubeProcType type = std::get<1>(group);
            uint32_t cascadeIndex = std::get<3>(group);
            if (type == ECubeProcType::ECPT_Fence)
            {
                if (!TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks))
                {
                    break; 
                }
                needUpdateGroups.pop();
                continue;
            }
            AsyncProcessGroup(std::get<0>(group).x, std::get<0>(group).z, scene, std::get<1>(group), std::get<2>(group), cascadeIndex);
            needUpdateGroups.pop();
        }
    }
}

void FCPUAccelerationStructure::RequestUpdate(vec3 worldPos, float radius)
{
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        ivec3 center = ivec3((worldPos - baker.CUBE_OFFSET) / baker.UNIT_SIZE);
        const int radiusInCells = static_cast<int>(radius / baker.UNIT_SIZE);
        ivec3 min = center - ivec3(radiusInCells);
        ivec3 max = center + ivec3(radiusInCells);

        for (int x = min.x; x <= max.x; ++x)
        {
            for (int z = min.z; z <= max.z; ++z)
            {
                ivec3 point(x, 1, z);
                needUpdateGroups.push({point, ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe, cascadeIndex});
            }
        }
    }
}

void FCPUProbeBaker::ClearAmbientCubes()
{
    for(auto& voxel : voxels)
    {
        voxel = {};
    }
}

void FCPUPageIndex::Init()
{
    pageIndex.resize(Assets::ACGI_PAGE_COUNT * Assets::ACGI_PAGE_COUNT);
}

void FCPUPageIndex::UpdateData(const std::vector<FCPUProbeBaker>& bakers)
{
    // 粗暴实现，先全部page置空
    for (auto& page : pageIndex)
    {
        page = {};
        page.voxelCount = 0;
    }

    // 聚合所有cascade，构建全局PageIndex覆盖
    for (const FCPUProbeBaker& baker : bakers)
    {
        const uint32_t voxelCount = static_cast<uint32_t>(baker.voxels.size());
        for (uint32_t gIdx = 0; gIdx < voxelCount; ++gIdx)
        {
            const VoxelData& voxel = baker.voxels[gIdx];
            if (voxel.matId == 0)
            {
                continue;
            }

            // convert to local position
            uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
            uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
            uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

            vec3 worldPos = vec3(x, y, z) * baker.UNIT_SIZE + baker.CUBE_OFFSET;

            Assets::PageIndex& page = GetPage(worldPos);

            // 当前仅使用voxelCount>0做粗粒度裁剪，保持占用标记语义即可
            page.voxelCount = 1;
        }
    }
}

Assets::PageIndex& FCPUPageIndex::GetPage(glm::vec3 worldpos)
{
    // 假设CUBE_OFFSET定义了世界空间的起始位置
    glm::vec3 relativePos = worldpos - Assets::ACGI_PAGE_OFFSET;

    // 计算页面索引，假设每个page对应PAGE_UNIT的世界空间距离
    // 使用xz平面进行映射
    int pageX = static_cast<int>(relativePos.x / Assets::ACGI_PAGE_SIZE);
    int pageZ = static_cast<int>(relativePos.z / Assets::ACGI_PAGE_SIZE);

    // 限制在有效范围内
    pageX = glm::clamp(pageX, 0, Assets::ACGI_PAGE_COUNT - 1);
    pageZ = glm::clamp(pageZ, 0, Assets::ACGI_PAGE_COUNT - 1);

    // 计算一维索引
    int index = pageZ * Assets::ACGI_PAGE_COUNT + pageX;

    // 返回对应的PageIndex引用
    return pageIndex[index];
}

void FCPUPageIndex::UploadGPU(Vulkan::DeviceMemory& gpuMemory)
{
    PageIndex* data = reinterpret_cast<PageIndex*>(gpuMemory.Map(0, sizeof(PageIndex) * pageIndex.size()));
    std::memcpy(data, pageIndex.data(), pageIndex.size() * sizeof(PageIndex));
    gpuMemory.Unmap();
}

void FCPUAccelerationStructure::GenShadowMap(Scene& scene)
{
    if (bvhInstanceList.empty())
    {
        return;
    }

    if (!scene.GetEnvSettings().HasSun)
    {
        return;
    }
    
    const vec3& sunDir = scene.GetEnvSettings().SunDirection();
    
    // 阴影图分辨率设置
    int shadowMapSize = SHADOWMAP_SIZE;
    int tileSize = 256; // 每个tile的大小
    int tilesPerRow = shadowMapSize / tileSize;
    shadowMapR32.resize(shadowMapSize * shadowMapSize, 0); // 初始化为1.0（不被遮挡）

    // 使用环境设置中的方法获取光源视图投影矩阵
    mat4 lightViewProj = scene.GetEnvSettings().GetSunViewProjection();
    mat4 invLVP = inverse(lightViewProj);
    vec3 lightDir = normalize(-sunDir);

    
    // 计算当前tile的起始像素坐标
    for ( int currentTileX = 0; currentTileX < tilesPerRow; ++currentTileX )
    {
        for ( int currentTileY = 0; currentTileY < tilesPerRow; ++currentTileY )
        {
            int startX = currentTileX * tileSize;
            int startY = currentTileY * tileSize;

                // 处理当前tile
            TaskCoordinator::GetInstance()->AddParralledTask(
                [this, lightViewProj, invLVP, lightDir, startX, startY, tileSize, shadowMapSize](ResTask& task)
                {
                    for (int y = 0; y < tileSize; y++)
                    {
                        for (int x = 0; x < tileSize; x++)
                        {
                            int pixelX = startX + x;
                            int pixelY = startY + y;
                            
                            // 计算NDC坐标
                            float ndcX = (pixelX / static_cast<float>(shadowMapSize - 1)) * 2.0f - 1.0f;
                            float ndcY = 1.0f - (pixelY / static_cast<float>(shadowMapSize - 1)) * 2.0f;
                            
                            // 从NDC空间变换到世界空间
                            vec4 worldPos = invLVP * vec4(ndcX, ndcY, 0.0f, 1.0f);
                            worldPos /= worldPos.w;
                            
                            // 发射光线
                            vec3 origin = vec3(worldPos);
                            vec3 rayDir = normalize(lightDir);
                            
                            tinybvh::Ray ray(
                                tinybvh::bvhvec3(origin.x, origin.y, origin.z),
                                tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z),
                                10000.0f
                            );
                            
                            GCpuBvh.Intersect(ray);
                            if (ray.hit.t < 9999.0f)
                            {
                                vec3 hitPoint = origin + rayDir * ray.hit.t;
                                vec4 hitPosInLightSpace = lightViewProj * vec4(hitPoint, 1.0f);
                                float depth = (hitPosInLightSpace.z / hitPosInLightSpace.w + 1.0f) * 0.5f;
                                shadowMapR32[pixelY * shadowMapSize + pixelX] = depth;
                            }
                        }
                    }
                },
                [this, &scene, shadowMapSize, startX, startY, tileSize](ResTask& task)
                {
                    // 更新当前tile到GPU
                    Vulkan::CommandPool& commandPool = GlobalTexturePool::GetInstance()->GetMainThreadCommandPool();
                    const unsigned char* tileData = reinterpret_cast<const unsigned char*>(shadowMapR32.data());
                    scene.ShadowMap().UpdateDataMainThread(commandPool, startX, startY, tileSize, tileSize, shadowMapSize, shadowMapSize,
                        tileData, shadowMapSize * shadowMapSize * sizeof(float));
                }
            );
        }
    }
}
