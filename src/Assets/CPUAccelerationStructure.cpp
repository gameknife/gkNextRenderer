#include "CPUAccelerationStructure.h"
#include "Runtime/TaskCoordinator.hpp"
#include "Vulkan/DeviceMemory.hpp"
#include "Scene.hpp"
#include <chrono>
#include <fstream>

#define TINYBVH_IMPLEMENTATION
#include "Runtime/Engine.hpp"
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include "Utilities/Math.hpp"

static tinybvh::BVH GCpuBvh;
static std::vector<tinybvh::BLASInstance>* GbvhInstanceList;
static std::vector<FCPUTLASInstanceInfo>* GbvhTLASContexts;
static std::vector<FCPUBLASContext>* GbvhBLASContexts;
static std::vector<Assets::AmbientCube>* GCubes;
Assets::SphericalHarmonics HDRSHs[100];
using namespace glm;
using namespace Assets;

#include "../assets/shaders/common/SampleIBL.glsl"
#include "../assets/shaders/common/AmbientCubeCommon.glsl"

vec3 AlignWithNormal(vec3 ray, vec3 normal)
{
    glm::vec3 up = glm::abs(normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
    glm::mat3 transform(tangent, bitangent, normal);
    return transform * ray;
}

float RandomFloat(uvec4& v)
{
    // Wang hash for pseudo-random number generation
    v.x = (v.x ^ 61) ^ (v.x >> 16);
    v.x = v.x + (v.x << 3);
    v.x = v.x ^ (v.x >> 4);
    v.x = v.x * 0x27d4eb2d;
    v.x = v.x ^ (v.x >> 15);
    
    // Convert uint to float in range [0,1)
    return float(v.x) / float(0xFFFFFFFF);
}

int TracingOccludeFunction(vec3 origin, vec3 lightPos)
{
    glm::vec3 direction = lightPos - origin;
    float length = glm::length(direction) - 0.02f;
    tinybvh::Ray shadowRay(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(direction.x, direction.y, direction.z), length);
    return GCpuBvh.IsOccluded(shadowRay) ? 0 : 1;
}

bool TracingFunction(vec3 origin, vec3 rayDir, vec3& OutNormal, uint& OutMaterialId, float& OutRayDist, uint& OutInstanceId )
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z),
                             tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), 11.f);

    GCpuBvh.Intersect(ray);

    if (ray.hit.t < 10.0f)
    {
        uint32_t primIdx = ray.hit.prim;
        tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
        FCPUTLASInstanceInfo& instContext = (*GbvhTLASContexts)[ray.hit.inst];
        FCPUBLASContext& context = (*GbvhBLASContexts)[instance.blasIdx];
        mat4* worldTS = (mat4*)instance.transform;
        vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;

        OutRayDist = ray.hit.t;
        OutNormal = vec3(normalWS.x, normalWS.y, normalWS.z);
        OutMaterialId = context.extinfos[primIdx].matIdx;
        OutInstanceId = ray.hit.inst;
        return true;
    }
    else
    {
        return false;
    }
}

AmbientCube& FetchCube(ivec3 probePos)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY +
    probePos.z * CUBE_SIZE_XY + probePos.x;
    return (*GCubes)[idx];
}

vec4 FetchDirectLight(vec3 hitPos, vec3 normal, uint OutMaterialId, uint OutInstanceId)
{
    uint32_t diffuseColor = (*GbvhTLASContexts)[OutInstanceId].mats[OutMaterialId];
    vec4 albedo = unpackUnorm4x8(diffuseColor);
    
    vec3 pos = (hitPos - CUBE_OFFSET) / CUBE_UNIT;
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE_XY || pos.y > CUBE_SIZE_Z || pos.z > CUBE_SIZE_XY) {
        return vec4(1.0);
    }
    
    ivec3 baseIdx = ivec3(floor(pos));
    vec3 frac = fract(pos);
    
    float totalWeight = 0.0;
    vec4 result = vec4(0.0);

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(
        i & 1,
        (i >> 1) & 1,
        (i >> 2) & 1
        );

        ivec3 probePos = baseIdx + offset;
        AmbientCube cube = FetchCube(probePos);
        if (cube.Active != 1) continue;

        float wx = offset.x == 0 ? (1.0f - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0f - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0f - frac.z) : frac.z;
        float weight = wx * wy * wz;
        
        vec4 sampleColor = sampleAmbientCubeHL2_DI(cube, normal);
        result += sampleColor * weight;
        totalWeight += weight;
    }
    
    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05f);
    return albedo * indirectColor;
}

#include "../assets/shaders/common/AmbientCubeAlgo.glsl"

void FCPUAccelerationStructure::InitBVH(Assets::Scene& scene)
{
    auto& HDR = Assets::GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
    std::memcpy(HDRSHs, HDR.data(), HDR.size() * sizeof(Assets::SphericalHarmonics));
    
    const auto timer = std::chrono::high_resolution_clock::now();

    bvhBLASList.clear();
    bvhBLASContexts.clear();

    bvhBLASContexts.resize(scene.Models().size());
    for ( size_t m = 0; m < scene.Models().size(); ++m )
    {
        const Assets::Model& model = scene.Models()[m];
        
        for (size_t i = 0; i < model.CPUIndices().size(); i += 3)
        {
            // Get the three vertices of the triangle
            const Assets::Vertex& v0 = model.CPUVertices()[model.CPUIndices()[i]];
            const Assets::Vertex& v1 = model.CPUVertices()[model.CPUIndices()[i + 1]];
            const Assets::Vertex& v2 = model.CPUVertices()[model.CPUIndices()[i + 2]];
            
            // Calculate face normal
            glm::vec3 edge1 = glm::vec3(v1.Position) - glm::vec3(v0.Position);
            glm::vec3 edge2 = glm::vec3(v2.Position) - glm::vec3(v1.Position);
            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));


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

    double elapsed = std::chrono::duration<float, std::chrono::milliseconds::period>(
    std::chrono::high_resolution_clock::now() - timer).count();

    // ugly code for global accesss here
    fmt::print("build bvh takes: {:.0f}ms\n", elapsed);

    ambientCubes.resize( Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z );
    ambientCubes_Copy.resize( Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z );
    
    UpdateBVH(scene);
}

void FCPUAccelerationStructure::UpdateBVH(Assets::Scene& scene)
{
    bvhInstanceList.clear();
    bvhTLASContexts.clear();

    for (auto& node : scene.Nodes())
    {
        node->RecalcTransform(true);

        uint32_t modelId = node->GetModel();
        if (modelId == -1) continue;

        glm::mat4 worldTS = node->WorldTransform();
        worldTS = transpose(worldTS);

        tinybvh::BLASInstance instance;
        instance.blasIdx = modelId;
        std::memcpy( (float*)instance.transform, &(worldTS[0]), sizeof(float) * 16);

        bvhInstanceList.push_back(instance);
        FCPUTLASInstanceInfo info;
        for ( int i = 0; i < node->Materials().size(); ++i )
        {
            uint32_t matId = node->Materials()[i];
            Assets::FMaterial& mat = scene.Materials()[matId];
            info.mats[i] = glm::packUnorm4x8( mat.gpuMaterial_.Diffuse );
        }
        bvhTLASContexts.push_back( info );
    }

    if (bvhInstanceList.size() > 0)
    {
        GCpuBvh.Build( bvhInstanceList.data(), static_cast<int>(bvhInstanceList.size()), bvhBLASList.data(), static_cast<int>(bvhBLASList.size()) );
    }

    // rebind with new address
    GbvhInstanceList = &bvhInstanceList;
    GbvhTLASContexts = &bvhTLASContexts;
    GbvhBLASContexts = &bvhBLASContexts;
    GCubes = &ambientCubes;
}

Assets::RayCastResult FCPUAccelerationStructure::RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir)
{
    Assets::RayCastResult Result;

    // tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z));
    // GCpuBvh.Intersect(ray);
    //
    // if (ray.hit.t < 100000.f)
    // {
    //     glm::vec3 hitPos = rayOrigin + rayDir * ray.hit.t;
    //
    //     Result.HitPoint = glm::vec4(hitPos, 0);
    //     Result.Normal = glm::vec4(GExtInfos[ray.hit.prim].normal, 0);
    //     Result.Hitted = true;
    //     Result.InstanceId = GExtInfos[ray.hit.prim].instanceId;
    // }

    return Result;
}

void FCPUAccelerationStructure::ProcessCube(int x, int y, int z, ECubeProcType procType)
{
    auto& ubo = NextEngine::GetInstance()->GetUniformBufferObject();
    glm::vec3 probePos = glm::vec3(x, y, z) * Assets::CUBE_UNIT + Assets::CUBE_OFFSET;
    uint32_t addressIdx = y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x;
    Assets::AmbientCube& cube = ambientCubes[addressIdx];

    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
            {
                
            }
            break;
        case ECubeProcType::ECPT_Iterate:
            {
                cube.Active = 1;
                cube.Lighting = 0;
                cube.ExtInfo = glm::uvec4(0, 0,0,0);

                uvec4 RandomSeed(0);
                vec4 bounceColor(0);
                vec4 skyColor(0);

                cube.PosY_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,1,0), cube.Active, bounceColor, skyColor, ubo) );
                cube.PosY = LerpPackedColorAlt( cube.PosY, bounceColor, 0.25f );
                cube.PosY_S = LerpPackedColorAlt( cube.PosY_S, skyColor, 0.25f );

                if (cube.Active == 0) return;

                // 负Y方向
                cube.NegY_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,-1,0), cube.Active, bounceColor, skyColor, ubo) );
                cube.NegY = LerpPackedColorAlt( cube.NegY, bounceColor, 0.25f );
                cube.NegY_S = LerpPackedColorAlt( cube.NegY_S, skyColor, 0.25f );

                if (cube.Active == 0) return;

                // 正X方向
                cube.PosX_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(1,0,0), cube.Active, bounceColor, skyColor, ubo) );
                cube.PosX = LerpPackedColorAlt( cube.PosX, bounceColor, 0.25f );
                cube.PosX_S = LerpPackedColorAlt( cube.PosX_S, skyColor, 0.25f );

                if (cube.Active == 0) return;

                // 负X方向
                cube.NegX_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(-1,0,0), cube.Active, bounceColor, skyColor, ubo) );
                cube.NegX = LerpPackedColorAlt( cube.NegX, bounceColor, 0.25f );
                cube.NegX_S = LerpPackedColorAlt( cube.NegX_S, skyColor, 0.25f );

                if (cube.Active == 0) return;

                // 正Z方向
                cube.PosZ_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,0,1), cube.Active, bounceColor, skyColor, ubo) );
                cube.PosZ = LerpPackedColorAlt( cube.PosZ, bounceColor, 0.25f );
                cube.PosZ_S = LerpPackedColorAlt( cube.PosZ_S, skyColor, 0.25f );

                if (cube.Active == 0) return;
    
                // 负Z方向
                cube.NegZ_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,0,-1), cube.Active, bounceColor, skyColor, ubo) );
                cube.NegZ = LerpPackedColorAlt( cube.NegZ, bounceColor, 0.25f );
                cube.NegZ_S = LerpPackedColorAlt( cube.NegZ_S, skyColor, 0.25f);
            }
            break;
        case ECubeProcType::ECPT_Copy:
            {
                ambientCubes_Copy[addressIdx] = ambientCubes[addressIdx];
            }
            break;
        case ECubeProcType::ECPT_Blur:
        {
            uint32_t centerIdx = y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x;
            Assets::AmbientCube& centerCube = ambientCubes[centerIdx];

            // 如果当前立方体不活跃，不进行模糊处理
            if (centerCube.Active != 1) return;

            // 定义采样权重和累积值
            float totalWeight = 0.0f;
            vec4 blurredPosX(0), blurredNegX(0);
            vec4 blurredPosY(0), blurredNegY(0);
            vec4 blurredPosZ(0), blurredNegZ(0);

            vec4 blurredPosX_S(0), blurredNegX_S(0);
            vec4 blurredPosY_S(0), blurredNegY_S(0);
            vec4 blurredPosZ_S(0), blurredNegZ_S(0);

            // 在3x3x3的区域中采样
            for (int dy = -1; dy <= 1; dy++) {
                for (int dz = -1; dz <= 1; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx;
                        int ny = y + dy;
                        int nz = z + dz;

                        // 检查边界
                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= Assets::CUBE_SIZE_XY ||
                            ny >= Assets::CUBE_SIZE_Z ||
                            nz >= Assets::CUBE_SIZE_XY) {
                            continue;
                        }

                        uint32_t neighborIdx = ny * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + nz * Assets::CUBE_SIZE_XY + nx;
                        Assets::AmbientCube& neighborCube = ambientCubes_Copy[neighborIdx];

                        // 如果邻居立方体不活跃，跳过
                        if (neighborCube.Active != 1) continue;

                        // 计算权重 (距离越远权重越小)
                        float dist = glm::length(glm::vec3(dx, dy, dz));
                        float weight = 1.0f / (1.0f + dist);

                        // 解包颜色值并累积加权颜色
                        blurredPosX += unpackRGB10A2(neighborCube.PosX) * weight;
                        blurredNegX += unpackRGB10A2(neighborCube.NegX) * weight;
                        blurredPosY += unpackRGB10A2(neighborCube.PosY) * weight;
                        blurredNegY += unpackRGB10A2(neighborCube.NegY) * weight;
                        blurredPosZ += unpackRGB10A2(neighborCube.PosZ) * weight;
                        blurredNegZ += unpackRGB10A2(neighborCube.NegZ) * weight;

                        blurredPosX_S += unpackRGB10A2(neighborCube.PosX_S) * weight;
                        blurredNegX_S += unpackRGB10A2(neighborCube.NegX_S) * weight;
                        blurredPosY_S += unpackRGB10A2(neighborCube.PosY_S) * weight;
                        blurredNegY_S += unpackRGB10A2(neighborCube.NegY_S) * weight;
                        blurredPosZ_S += unpackRGB10A2(neighborCube.PosZ_S) * weight;
                        blurredNegZ_S += unpackRGB10A2(neighborCube.NegZ_S) * weight;

                        totalWeight += weight;
                    }
                }
            }

            // 如果有有效的邻居，计算平均值并更新当前立方体
            if (totalWeight > 0.0f) {
                float invWeight = 1.0f / totalWeight;

                // 归一化并重新打包颜色值
                centerCube.PosX = packRGB10A2(blurredPosX * invWeight);
                centerCube.NegX = packRGB10A2(blurredNegX * invWeight);
                centerCube.PosY = packRGB10A2(blurredPosY * invWeight);
                centerCube.NegY = packRGB10A2(blurredNegY * invWeight);
                centerCube.PosZ = packRGB10A2(blurredPosZ * invWeight);
                centerCube.NegZ = packRGB10A2(blurredNegZ * invWeight);

                centerCube.PosX_S = packRGB10A2(blurredPosX_S * invWeight);
                centerCube.NegX_S = packRGB10A2(blurredNegX_S * invWeight);
                centerCube.PosY_S = packRGB10A2(blurredPosY_S * invWeight);
                centerCube.NegY_S = packRGB10A2(blurredNegY_S * invWeight);
                centerCube.PosZ_S = packRGB10A2(blurredPosZ_S * invWeight);
                centerCube.NegZ_S = packRGB10A2(blurredNegZ_S * invWeight);
            }
        }
        break;
    }
}

void FCPUAccelerationStructure::AsyncProcessFull()
{
    return;
    needUpdateGroups.clear();
    lastBatchTasks.clear();
    TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    ClearAmbientCubes();

    // 目前的做法，只能以1m为单位，不过其实也够了
    int groupSize = static_cast<int>(std::round(1.0f / Assets::CUBE_UNIT));
    int lengthX = Assets::CUBE_SIZE_XY / groupSize;
    int lengthZ = Assets::CUBE_SIZE_XY / groupSize;

    // add 4 pass
    for(int pass = 0; pass < 4; ++pass)
    {
        // 先创建所有坐标对
        std::vector<std::pair<int, int>> coordinates;
        for (int x = 0; x < lengthX - 1; x++) {
            for (int z = 0; z < lengthZ - 1; z++) {
                coordinates.push_back({x, z});
            }
        }

        // 随机打乱坐标顺序
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(coordinates.begin(), coordinates.end(), g);

        // 按打乱后的顺序添加任务
        for (const auto& [x, z] : coordinates) {
            needUpdateGroups.push_back({glm::ivec3(x, 0, z), ECubeProcType::ECPT_Iterate});
        }

        // add 1 copy pass
        for (int x = 0; x < lengthX - 1; x++)
        {
            for (int z = 0; z < lengthZ - 1; z++)
            {
                needUpdateGroups.push_back({glm::ivec3(x, 0, z), ECubeProcType::ECPT_Copy});
            }
        }

        // add 1 blur pass
        for (int x = 0; x < lengthX - 1; x++)
        {
            for (int z = 0; z < lengthZ - 1; z++)
            {
                needUpdateGroups.push_back({glm::ivec3(x, 0, z), ECubeProcType::ECPT_Blur});
            }
        }
    }
}

void FCPUAccelerationStructure::AsyncProcessGroup(int xInMeter, int zInMeter, Assets::Scene& scene, ECubeProcType procType)
{
    if (bvhInstanceList.empty())
    {
        return;
    }
    
    int groupSize = static_cast<int>(std::round(1.0f / Assets::CUBE_UNIT));
    
    int actualX = xInMeter * groupSize;
    int actualZ = zInMeter * groupSize;

    std::vector<glm::vec3> lightPos;
    std::vector<glm::vec3> sunDir;
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
                    for (int y = 0; y < Assets::CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                            ProcessCube(x, y, z, procType);
            },
            [this](ResTask& task)
            {
                needFlush = true;
            });

    lastBatchTasks.push_back(taskId);
}

void FCPUAccelerationStructure::Tick(Assets::Scene& scene, Vulkan::DeviceMemory* GPUMemory)
{
    if (needFlush)
    {
        // Upload to GPU, now entire range
        Assets::AmbientCube* data = reinterpret_cast<Assets::AmbientCube*>(GPUMemory->Map(0, sizeof(Assets::AmbientCube) * ambientCubes.size()));
        std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(Assets::AmbientCube));
        GPUMemory->Unmap();

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
        if(!needUpdateGroups.empty())
        {
            // if we got upadte task, rebuild bvh and dispatch
            UpdateBVH(scene);

            for( auto& group : needUpdateGroups)
            {
                AsyncProcessGroup(std::get<0>(group).x, std::get<0>(group).z, scene, std::get<1>(group));
            }

            needUpdateGroups.clear();
        }
    }
}

void FCPUAccelerationStructure::RequestUpdate(glm::vec3 worldPos, float radius)
{
    glm::ivec3 center = glm::ivec3(worldPos - Assets::CUBE_OFFSET);
    glm::ivec3 min = center - glm::ivec3(static_cast<int>(radius));
    glm::ivec3 max = center + glm::ivec3(static_cast<int>(radius));

    // Insert all points within the radius (using manhattan distance for simplicity)
    for (int x = min.x; x <= max.x; ++x) {
        for (int z = min.z; z <= max.z; ++z) {
            glm::ivec3 point(x, 1, z);
            needUpdateGroups.push_back({point, ECubeProcType::ECPT_Iterate});
        }
    }
}

void FCPUAccelerationStructure::ClearAmbientCubes()
{
    for(auto& cube : ambientCubes)
    {
        cube.Active = 1;
        cube.Lighting = 0;
        cube.ExtInfo = glm::uvec4(0, 0, 0, 0);

        // 清理所有面的颜色
        uint32_t packedColor = packRGB10A2(vec4(0, 0, 0, 1));

        // 正Z面
        cube.PosZ = packedColor;
        cube.PosZ_S = packedColor;
        cube.PosZ_D = packedColor;

        // 负Z面
        cube.NegZ = packedColor;
        cube.NegZ_S = packedColor;
        cube.NegZ_D = packedColor;

        // 正Y面
        cube.PosY = packedColor;
        cube.PosY_S = packedColor;
        cube.PosY_D = packedColor;

        // 负Y面
        cube.NegY = packedColor;
        cube.NegY_S = packedColor;
        cube.NegY_D = packedColor;

        // 正X面
        cube.PosX = packedColor;
        cube.PosX_S = packedColor;
        cube.PosX_D = packedColor;

        // 负X面
        cube.NegX = packedColor;
        cube.NegX_S = packedColor;
        cube.NegX_D = packedColor;
    }

    needFlush = true;
}
