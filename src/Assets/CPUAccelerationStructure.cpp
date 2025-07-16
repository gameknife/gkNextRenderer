#include "CPUAccelerationStructure.h"
#include "Runtime/TaskCoordinator.hpp"
#include "Vulkan/DeviceMemory.hpp"
#include "Scene.hpp"
#include <chrono>
#include <fstream>

#define TINYBVH_IMPLEMENTATION
#include "TextureImage.hpp"
#include "Runtime/Engine.hpp"
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include "Utilities/Math.hpp"

static tinybvh::BVH GCpuBvh;
static std::vector<tinybvh::BLASInstance>* GbvhInstanceList;
static std::vector<FCPUTLASInstanceInfo>* GbvhTLASContexts;
static std::vector<FCPUBLASContext>* GbvhBLASContexts;

Assets::SphericalHarmonics HDRSHs[100];

using namespace Assets;

uint pack_bytes(glm::u32vec4 values)
{
    return (values.x & 0xFF) |
           ((values.y & 0xFF) << 8) |
           ((values.z & 0xFF) << 16) |
           ((values.w & 0xFF) << 24);
}

FMaterial& FetchMaterial(uint matId)
{
    auto& materials = NextEngine::GetInstance()->GetScene().Materials();
    return materials[matId];
}

uint FetchMaterialId(uint MaterialIdx, uint InstanceId)
{
    return (*GbvhTLASContexts)[InstanceId].matIdxs[MaterialIdx];
}

bool TraceRay(vec3 origin, vec3 rayDir, float Dist, vec3& OutNormal, uint& OutMaterialId, float& OutRayDist, uint& OutInstanceId )
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), Dist);
    GCpuBvh.Intersect(ray);

    if (ray.hit.t < Dist)
    {
        uint32_t primIdx = ray.hit.prim;
        tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
        FCPUTLASInstanceInfo& instContext = (*GbvhTLASContexts)[ray.hit.inst];
        FCPUBLASContext& context = (*GbvhBLASContexts)[instance.blasIdx];
        mat4* worldTS = (mat4*)instance.transform;
        vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;

        OutRayDist = ray.hit.t;
        OutNormal = vec3(normalWS.x, normalWS.y, normalWS.z);
        OutMaterialId =  FetchMaterialId( context.extinfos[primIdx].matIdx, ray.hit.inst );
        OutInstanceId = instContext.nodeId;
        return true;
    }
    
    return false;
}

#define float2 vec2
#define float3 vec3
#define float4 vec4

bool InsideGeometry( float3& origin, float3 rayDir, VoxelData& OutCube, float& distance)
{
    // 求交测试
    vec3 OutNormal;
    float OutRayDist;
    uint TempMaterialId;
    uint TempInstanceId;

    if (TraceRay(origin, rayDir, CUBE_UNIT, OutNormal, TempMaterialId, OutRayDist, TempInstanceId))
    {
        distance = OutRayDist;
        FMaterial hitMaterial = FetchMaterial(TempMaterialId);
        OutCube.matId = TempMaterialId;
        
        // 命中反面，识别为固体，并将lightprobe推出体外
        if (dot(OutNormal, rayDir) > 0.0 || ((hitMaterial.gpuMaterial_.MaterialModel == Material::Enum::DiffuseLight) && OutRayDist < 0.02f))
        {
            distance = 0;
            return true;
        }
    }
    return false;
}

void VoxelizeCube(VoxelData& Cube, float3 origin)
{
    // just write matid and solid status
    Cube.age = 0;
    Cube.matId = 0;

    float distPY = 255.0f;
    float distNY = 255.0f;
    float distPX = 255.0f;
    float distNX = 255.0f;
    float distPZ = 255.0f;
    float distNZ = 255.0f;
    
    InsideGeometry(origin, float3(0, 1, 0), Cube, distPY);
    InsideGeometry(origin, float3(0, -1, 0), Cube, distNY);
    InsideGeometry(origin, float3(1, 0, 0), Cube, distPX);
    InsideGeometry(origin, float3(-1, 0, 0), Cube, distNX);
    InsideGeometry(origin, float3(0, 0, 1), Cube, distPZ);
    InsideGeometry(origin, float3(0, 0, -1), Cube, distNZ);
    
    distPY = glm::fclamp(distPY * 4.0f, 0.0f, 1.0f);
    distNY = glm::fclamp(distNY * 4.0f, 0.0f, 1.0f);
    distPX = glm::fclamp(distPX * 4.0f, 0.0f, 1.0f);
    distNX = glm::fclamp(distNX * 4.0f, 0.0f, 1.0f);
    distPZ = glm::fclamp(distPZ * 4.0f, 0.0f, 1.0f);
    distNZ = glm::fclamp(distNZ * 4.0f, 0.0f, 1.0f);

    float inside = distPY * distNY * distPX * distNX * distPZ * distNZ;

    Cube.distanceToSolid_gg_z01 = pack_bytes(glm::u32vec4(0, uint(inside * 255.0f), uint(distPZ * 255.0f), uint(distNZ * 255.0f)));
    Cube.distanceToSolid_x01_y01 = pack_bytes(glm::u32vec4(uint(distPX * 255.0f), uint(distNX * 255.0f), uint(distPY * 255.0f), uint(distNY * 255.0f)));
}

#undef float2
#undef float3
#undef float4

void FCPUProbeBaker::Init(float unit_size, vec3 offset)
{
    UNIT_SIZE = unit_size;
    CUBE_OFFSET = offset;
    voxels.resize( CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z );
}

void FCPUAccelerationStructure::InitBVH(Scene& scene)
{
    auto& HDR = GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
    std::memcpy(HDRSHs, HDR.data(), HDR.size() * sizeof(SphericalHarmonics));
    
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
        bvhBLASContexts[m].bvh.Build( bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
        bvhBLASList.push_back( &bvhBLASContexts[m].bvh );
    }
    
    probeBaker.Init( CUBE_UNIT, CUBE_OFFSET );
    cpuPageIndex.Init();

    UpdateBVH(scene);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    std::vector<tinybvh::BLASInstance> tmpbvhInstanceList;
    std::vector<FCPUTLASInstanceInfo> tmpbvhTLASContexts;

    for (auto& node : scene.Nodes())
    {
        node->RecalcTransform(true);

        uint32_t modelId = node->GetModel();
        if (modelId == -1) continue;

        mat4 worldTS = node->WorldTransform();
        worldTS = transpose(worldTS);

        tinybvh::BLASInstance instance;
        instance.blasIdx = modelId;
        std::memcpy( (float*)instance.transform, &(worldTS[0]), sizeof(float) * 16);

        tmpbvhInstanceList.push_back(instance);
        FCPUTLASInstanceInfo info;
        for ( int i = 0; i < node->Materials().size(); ++i )
        {
            uint32_t matId = node->Materials()[i];
            FMaterial& mat = scene.Materials()[matId];
            info.matIdxs[i] = matId;
            info.nodeId = node->GetInstanceId();
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
    GbvhTLASContexts = &bvhTLASContexts;
    GbvhBLASContexts = &bvhBLASContexts;
}

RayCastResult FCPUAccelerationStructure::RayCastInCPU(vec3 rayOrigin, vec3 rayDir)
{
    RayCastResult Result;

    tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), 2000.0f);
    GCpuBvh.Intersect(ray);
    
    if (ray.hit.t < 2000.f)
    {
        vec3 hitPos = rayOrigin + rayDir * ray.hit.t;
        uint32_t primIdx = ray.hit.prim;
        tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
        FCPUTLASInstanceInfo& instContext = (*GbvhTLASContexts)[ray.hit.inst];
        FCPUBLASContext& context = (*GbvhBLASContexts)[instance.blasIdx];
        mat4* worldTS = (mat4*)instance.transform;
        vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;
        Result.HitPoint = vec4(hitPos, 0);
        Result.Normal = normalWS;
        Result.Hitted = true;
        Result.T = ray.hit.t;
        Result.InstanceId = ray.hit.inst;
    }

    return Result;
}

void FCPUProbeBaker::ProcessCube(int x, int y, int z, ECubeProcType procType)
{
    auto& ubo = NextEngine::GetInstance()->GetUniformBufferObject();
    vec3 probePos = vec3(x, y, z) * UNIT_SIZE + CUBE_OFFSET;
    uint32_t addressIdx = y * CUBE_SIZE_XY * CUBE_SIZE_XY + z * CUBE_SIZE_XY + x;
    VoxelData& voxel = voxels[addressIdx];
        
    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
        case ECubeProcType::ECPT_Fence:
            break;
        case ECubeProcType::ECPT_Voxelize:
            VoxelizeCube(voxel, probePos);
            break;
    }
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& VoxelGPUMemory)
{
    VoxelData* data = reinterpret_cast<VoxelData*>(VoxelGPUMemory.Map(0, sizeof(VoxelData) * voxels.size()));
    std::memcpy(data, voxels.data(), voxels.size() * sizeof(VoxelData));
    VoxelGPUMemory.Unmap();
}

void FCPUAccelerationStructure::AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* VoxelGPUMemory, bool Incremental)
{    
    // clean
    while (!needUpdateGroups.empty())
        needUpdateGroups.pop();
    lastBatchTasks.clear();
    TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    
    if (!Incremental)
    {
        probeBaker.ClearAmbientCubes();
        probeBaker.UploadGPU(*VoxelGPUMemory);
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
        for (const auto& [x, z] : coordinates)
            needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe});
        // add fence
        needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe});
    }
}

void FCPUAccelerationStructure::AsyncProcessGroup(int xInMeter, int zInMeter, Scene& scene, ECubeProcType procType, EBakerType bakerType)
{
    if (bvhInstanceList.empty())
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
                [this, actualX, actualZ, groupSize, procType](ResTask& task)
            {
                for (int z = actualZ; z < actualZ + groupSize; z++)
                    for (int y = 0; y < CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                        {
                            probeBaker.ProcessCube(x, y, z, procType);
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

void FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* GPUMemory, Vulkan::DeviceMemory* VoxelGPUMemory, Vulkan::DeviceMemory* PageIndexMemory)
{
    if (needFlush)
    {
        // Upload to GPU, now entire range, optimize to partial upload later
        probeBaker.UploadGPU(*VoxelGPUMemory);
        cpuPageIndex.UpdateData(probeBaker);
        cpuPageIndex.UploadGPU(*PageIndexMemory);
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
            if (type == ECubeProcType::ECPT_Fence)
            {
                if (!TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks))
                {
                    break; 
                }
                needUpdateGroups.pop();
                continue;
            }
            AsyncProcessGroup(std::get<0>(group).x, std::get<0>(group).z, scene, std::get<1>(group), std::get<2>(group));
            needUpdateGroups.pop();
        }
    }
}

void FCPUAccelerationStructure::RequestUpdate(vec3 worldPos, float radius)
{
    ivec3 center = ivec3(worldPos - CUBE_OFFSET);
    ivec3 min = center - ivec3(static_cast<int>(radius));
    ivec3 max = center + ivec3(static_cast<int>(radius));

    // Insert all points within the radius (using manhattan distance for simplicity)
    for (int x = min.x; x <= max.x; ++x) {
        for (int z = min.z; x <= max.z; ++z) {
            ivec3 point(x, 1, z);
            needUpdateGroups.push({point, ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe});
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

void FCPUPageIndex::UpdateData(FCPUProbeBaker& baker)
{
    // 粗暴实现，先全部page置空
    for (auto& page : pageIndex)
    {
        page = {};
        page.voxelCount = 0;
    }

    // 遍历baker里的数据，根据index，取得worldpos，然后取page出来，给voxel数量提升
    for ( uint gIdx = 0; gIdx < baker.voxels.size(); ++gIdx)
    {
        VoxelData& voxel = baker.voxels[gIdx];
        if (voxel.matId == 0) continue; // 只处理活跃的cube

        // convert to local position
        uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
        uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
        uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

        vec3 worldPos = vec3(x, y, z) *  CUBE_UNIT + CUBE_OFFSET;

        // 获取对应的page
        Assets::PageIndex& page = GetPage(worldPos);

        // 增加voxel数量
        page.voxelCount += 1; // 假设每个cube对应一个voxel
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

void FCPUPageIndex::UploadGPU(Vulkan::DeviceMemory& GPUMemory)
{
    PageIndex* data = reinterpret_cast<PageIndex*>(GPUMemory.Map(0, sizeof(PageIndex) * pageIndex.size()));
    std::memcpy(data, pageIndex.data(), pageIndex.size() * sizeof(PageIndex));
    GPUMemory.Unmap();
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
