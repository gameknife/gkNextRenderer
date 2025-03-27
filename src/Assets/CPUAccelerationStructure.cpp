#include "CPUAccelerationStructure.h"
#include "Runtime/TaskCoordinator.hpp"
#include "Vulkan/DeviceMemory.hpp"
#include "Scene.hpp"
#include <chrono>
#include <fstream>

#define TINYBVH_IMPLEMENTATION
#include "Runtime/Engine.hpp"
#include "ThirdParty/tinybvh/tiny_bvh.h"

static tinybvh::BVH GCpuBvh;
static std::vector<tinybvh::BLASInstance>* GbvhInstanceList;
static std::vector<FCPUTLASInstanceInfo>* GbvhTLASContexts;
static std::vector<FCPUBLASContext>* GbvhBLASContexts;
Assets::SphericalHarmonics HDRSHs[100];
using namespace glm;
#include "../assets/shaders/common/SampleIBL.glsl"
#include "../assets/shaders/common/AmbientCubeCommon.glsl"

// void Onb(vec3 n, out vec3 b1, out vec3 b2) {
//     float signZ = n.z < 0.f ? -1.f : 1.f;
//     float a = -1.0f / (signZ + n.z);
//     b2 = vec3(n.x * n.y * a, signZ + n.y * n.y * a, -n.y);
//     b1 = vec3(1.0f + signZ * n.x * n.x * a, signZ * b2.x, -signZ * n.x);
// }

vec3 AlignWithNormal(vec3 ray, vec3 normal)
{
    glm::vec3 up = glm::abs(normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

    // Transform matrix from Z-up to normal-up space
    glm::mat3 transform(tangent, bitangent, normal);
    
    return transform * ray;
}

float RandomFloat(uvec4& v)
{
    return 1.0f;
}

int TracingOccludeFunction(vec3 origin, vec3 lightPos)
{
    return 0;
}

// return if hits, this function may differ between Shader & Cpp
bool TracingFunction(vec3 origin, vec3 rayDir, vec3& OutNormal, uint& OutMaterialId, float& OutRayDist)
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
        glm::mat4* worldTS = ( glm::mat4*)instance.transform;
        glm::vec4 normalWS = glm::vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;

        OutNormal = glm::vec3(normalWS.x, normalWS.y, normalWS.z);
        OutMaterialId = context.extinfos[primIdx].matIdx;
        
        return true;
    }
    else
    {
        return false;
    }
}

vec4 FetchDirectLight(vec3 hitPos, vec3 OutNormal, uint OutMaterialId)
{
    return vec4(1,1,1,1);
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
    GbvhInstanceList = &bvhInstanceList;
    GbvhTLASContexts = &bvhTLASContexts;
    GbvhBLASContexts = &bvhBLASContexts;
    
    fmt::print("build bvh takes: {:.0f}ms\n", elapsed);

    UpdateBVH(scene);

    ambientCubes.resize( Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z );
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

const glm::vec3 directions[6] = {
    glm::vec3(0, 0, 1), // PosZ
    glm::vec3(0, 0, -1), // NegZ
    glm::vec3(0, 1, 0), // PosY
    glm::vec3(0, -1, 0), // NegY
    glm::vec3(1, 0, 0), // PosX
    glm::vec3(-1, 0, 0) // NegX
};

uint packRGB10A2(vec4 color) {
    vec4 clamped = clamp(color / MAX_ILLUMINANCE, vec4(0.0f), vec4(1.0f) );
    
    uint r = uint(clamped.r * 1023.0f);
    uint g = uint(clamped.g * 1023.0f);
    uint b = uint(clamped.b * 1023.0f);
    uint a = uint(clamped.a * 3.0f);
    
    return r | (g << 10) | (b << 20) | (a << 30);
}

vec4 unpackRGB10A2(uint packed) {
    float r = float((packed) & 0x3FF) / 1023.0f;
    float g = float((packed >> 10) & 0x3FF) / 1023.0f;
    float b = float((packed >> 20) & 0x3FF) / 1023.0f;
    
    return vec4(r,g,b,0.0) * MAX_ILLUMINANCE;
}

// Transform pre-calculated samples to align with given normal
void GetHemisphereSamples(const glm::vec3& normal, std::vector<glm::vec3>& result)
{
    result.reserve(FACE_TRACING);

    // Create orthonormal basis
    glm::vec3 up = glm::abs(normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

    // Transform matrix from Z-up to normal-up space
    glm::mat3 transform(tangent, bitangent, normal);

    // Transform each pre-calculated sample
    for (const auto& sample : hemisphereVectors)
    {
        result.push_back(transform * sample);
    }
}

bool IsInShadow(const glm::vec3& point, const glm::vec3& lightPos, const tinybvh::BVH& bvh)
{
    glm::vec3 direction = lightPos - point;
    float length = glm::length(direction) - 0.02f;
    tinybvh::Ray shadowRay(tinybvh::bvhvec3(point.x, point.y, point.z), tinybvh::bvhvec3(direction.x, direction.y, direction.z), length);

    return bvh.IsOccluded(shadowRay);
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

    cube.PosY = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,1,0), cube.Active, bounceColor, skyColor, ubo) );
    //cube.PosY_D = packRGB10A2( directColor );
    cube.PosY_S = packRGB10A2( skyColor );
    
    // 负Y方向
    cube.NegY = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,-1,0), cube.Active, bounceColor, skyColor, ubo) );
    //cube.NegY_D = packRGB10A2( directColor );
    cube.NegY_S = packRGB10A2( skyColor );
    
    // 正X方向
    cube.PosX = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(1,0,0), cube.Active, bounceColor, skyColor, ubo) );
    //cube.PosX_D = packRGB10A2( directColor );
    cube.PosX_S = packRGB10A2( skyColor );
    
    // 负X方向 
    cube.NegX = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(-1,0,0), cube.Active, bounceColor, skyColor, ubo) );
    //cube.NegX_D = packRGB10A2( directColor );
    cube.NegX_S = packRGB10A2( skyColor );
    
    // 正Z方向
    cube.PosZ = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,0,1), cube.Active, bounceColor, skyColor, ubo) );
    //cube.PosZ_D = packRGB10A2( directColor );
    cube.PosZ_S = packRGB10A2( skyColor );
    
    // 负Z方向
    cube.NegZ = packRGB10A2( TraceOcclusion( RandomSeed, probePos, vec3(0,0,-1), cube.Active, bounceColor, skyColor, ubo) );
    //cube.NegZ_D = packRGB10A2( directColor );
    cube.NegZ_S = packRGB10A2( skyColor );
   
    return;

    for (int face = 0; face < 6; face++)
    {
        glm::vec3 baseDir = directions[face];

        float occlusion = 0.0f;
        glm::vec4 rayColor = glm::vec4(0, 0, 0, 0);
        std::vector<glm::vec3> hemisphereSamples;
        GetHemisphereSamples(baseDir, hemisphereSamples);

        bool nextCube = false;
        for (int i = 0; i < FACE_TRACING; i++)
        {
            tinybvh::Ray ray(tinybvh::bvhvec3(probePos.x, probePos.y, probePos.z),
                             tinybvh::bvhvec3(hemisphereSamples[i].x, hemisphereSamples[i].y, hemisphereSamples[i].z), 11.f);

            GCpuBvh.Intersect(ray);

            if (ray.hit.t < 10.0f)
            {
                uint32_t primIdx = ray.hit.prim;
                tinybvh::BLASInstance& instance = bvhInstanceList[ray.hit.inst];
                FCPUTLASInstanceInfo& instContext = bvhTLASContexts[ray.hit.inst];
                FCPUBLASContext& context = bvhBLASContexts[instance.blasIdx];
                glm::mat4* worldTS = ( glm::mat4*)instance.transform;
                glm::vec4 normalWS = glm::vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;
                float dotProduct = glm::dot(hemisphereSamples[i], glm::vec3(normalWS));
                
                if (dotProduct < 0.0f)
                {
                    // get the hit pos
                    auto hitPos = ray.O + ray.D * ray.hit.t;
                    glm::vec3* glmPosPtr = (glm::vec3*)(&hitPos);
                    //float power = IsInShadow(*glmPosPtr, *glmPosPtr + lightDir * -1000.0f, GCpuBvh) ? 0.5f: 1.0f;
                    float power =5.0f;
                    // occlude, bounce color, fade by distance
                    uint32_t diffuseColor = instContext.mats[context.extinfos[primIdx].matIdx];
                    
                    rayColor += unpackUnorm4x8(diffuseColor) * power;// * glm::clamp(1.0f - ray.hit.t / 5.0f, 0.0f, 1.0f) * 0.5f;
                    occlusion += 1.0f;
                }
                else
                {
                    cube.Active = 0;
                    cube.Lighting = 0;
                    nextCube = true;
                    break;
                }
            }
            else
            {
                // hit the sky, sky color, with a ibl is better
                float skyatten = 10.0f;
                glm::vec4 skyColor = SampleIBL(0, hemisphereSamples[i], 0.f, 1.f) * skyatten;
                rayColor += skyColor;
            }
        }

        if (nextCube)
        {
            continue;
        }

        occlusion /= FACE_TRACING;
        rayColor /= FACE_TRACING;

        // for each light in the scene, ray hit the center, if not occlude, add rayColor
        for( auto& light : lightPos )
        {
            if( !IsInShadow( probePos, light, GCpuBvh) )
            {
                // ndotl + distance attenuation
                glm::vec3 lightDir = glm::normalize(light - probePos);
                float ndotl = glm::clamp(glm::dot(baseDir, lightDir), 0.0f, 1.0f);
                float distance = glm::length(light - probePos);
                float attenuation = 1.0f / (distance * distance);
                rayColor += vec4(2000.0f, 2000.0f, 2000.0f, 1.0f) * ndotl * attenuation;
            }
        }

        float visibility = 1.0f - occlusion;
        glm::vec4 indirectColor = rayColor;//glm::vec4(visibility);
        uint32_t packedColor = packRGB10A2(indirectColor);
        uint32_t skyColor = packRGB10A2(vec4(0,0,0,0));
        switch (face)
        {
        case 0:
            cube.PosZ = packedColor;
            cube.PosZ_S = skyColor;
            break;
        case 1:
            cube.NegZ = packedColor;
            cube.NegZ_S = skyColor;
            break;
        case 2:
            cube.PosY = packedColor;
            cube.PosY_S = skyColor;
            break;
        case 3:
            cube.NegY = packedColor;
            cube.NegY_S = skyColor;
            break;
        case 4:
            cube.PosX = packedColor;
            cube.PosX_S = skyColor;
            break;
        case 5:
            cube.NegX = packedColor;
            cube.NegX_S = skyColor;
            break;
        }
    }

    if (!sunDir.empty())
    {
        IsInShadow(probePos, probePos + sunDir[0] * 1000.0f, GCpuBvh) ? cube.Lighting = 0 : cube.Lighting = 1;
    }
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
            needUpdateGroups.insert(glm::ivec3(x, 0, z));
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
            needUpdateGroups.insert(point);
        }
    }
}

