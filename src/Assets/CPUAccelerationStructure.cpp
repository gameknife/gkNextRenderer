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

void FCPUAccelerationStructure::ProcessCube(int x, int y, int z, std::vector<glm::vec3> sunDir, std::vector<glm::vec3> lightPos)
{
    auto& ubo = NextEngine::GetInstance()->GetUniformBufferObject();
    glm::vec3 probePos = glm::vec3(x, y, z) * Assets::CUBE_UNIT + Assets::CUBE_OFFSET;
    Assets::AmbientCube& cube = ambientCubes[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x];

    cube.Active = 1;
    cube.Lighting = 0;
    cube.ExtInfo = glm::uvec4(0, 0,0,0);

    uvec4 RandomSeed(0);
    vec4 bounceColor(0);
    vec4 skyColor(0);

    cube.PosY_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,1,0), cube.Active, bounceColor, skyColor, ubo) );
    cube.PosY = packRGB10A2( bounceColor );
    cube.PosY_S = LerpPackedColorAlt( cube.PosY_S, skyColor, 0.5f);

    if (cube.Active == 0) return;
    
    // 负Y方向
    cube.NegY_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,-1,0), cube.Active, bounceColor, skyColor, ubo) );
    cube.NegY = packRGB10A2( bounceColor );
    cube.NegY_S = LerpPackedColorAlt( cube.NegY_S, skyColor, 0.5f);

    if (cube.Active == 0) return;
    
    // 正X方向
    cube.PosX_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(1,0,0), cube.Active, bounceColor, skyColor, ubo) );
    cube.PosX = packRGB10A2( bounceColor );
    cube.PosX_S = LerpPackedColorAlt( cube.PosX_S, skyColor, 0.5f);

    if (cube.Active == 0) return;
    
    // 负X方向 
    cube.NegX_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(-1,0,0), cube.Active, bounceColor, skyColor, ubo) );
    cube.NegX = packRGB10A2( bounceColor );
    cube.NegX_S = LerpPackedColorAlt( cube.NegX_S, skyColor, 0.5f);

    if (cube.Active == 0) return;
    
    // 正Z方向
    cube.PosZ_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,0,1), cube.Active, bounceColor, skyColor, ubo) );
    cube.PosZ = packRGB10A2( bounceColor );
    cube.PosZ_S = LerpPackedColorAlt( cube.PosZ_S, skyColor, 0.5f);

    if (cube.Active == 0) return;
    
    // 负Z方向
    cube.NegZ_D = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,0,-1), cube.Active, bounceColor, skyColor, ubo) );
    cube.NegZ = packRGB10A2( bounceColor );
    cube.NegZ_S = LerpPackedColorAlt( cube.NegZ_S, skyColor, 0.5f);
}

void FCPUAccelerationStructure::AsyncProcessFull()
{
    // add a full update tasks
    int groupSize = static_cast<int>(std::round(1.0f / Assets::CUBE_UNIT));
    int lengthX = Assets::CUBE_SIZE_XY / groupSize;
    int lengthZ = Assets::CUBE_SIZE_XY / groupSize;
    
    for (int x = 0; x < lengthX - 1; x++)
    {
        for (int z = 0; z < lengthZ - 1; z++)
        {   
            needUpdateGroups.push_back(glm::ivec3(x, 0, z));
        }
    }

    // 2nd pass, for second bounce
    for (int x = 0; x < lengthX - 1; x++)
    {
        for (int z = 0; z < lengthZ - 1; z++)
        {   
            needUpdateGroups.push_back(glm::ivec3(x, 0, z));
        }
    }
    
    //TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    //BlurAmbientCubes(ambientCubes, ambientCubesCopy);
}

void FCPUAccelerationStructure::AsyncProcessGroup(int xInMeter, int zInMeter, Assets::Scene& scene)
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
                [this, actualX, actualZ, groupSize, sunDir, lightPos](ResTask& task)
            {
                for (int z = actualZ; z < actualZ + groupSize; z++)
                    for (int y = 0; y < Assets::CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                            ProcessCube(x, y, z, sunDir, lightPos);
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
                AsyncProcessGroup(group.x, group.z, scene);
                //NextEngine::GetInstance()->DrawAuxPoint( glm::vec3(group) + Assets::CUBE_OFFSET, glm::vec4(1, 0, 0, 1), 2, 30 );
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
            needUpdateGroups.push_back(point);
        }
    }
}

