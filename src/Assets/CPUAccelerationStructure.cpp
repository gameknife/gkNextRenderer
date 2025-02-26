#include "CPUAccelerationStructure.h"
#include "Runtime/TaskCoordinator.hpp"
#include "Vulkan/DeviceMemory.hpp"
#include "Scene.hpp"
#include <chrono>
#include <fstream>

#define TINYBVH_IMPLEMENTATION
#include "Runtime/Engine.hpp"
#include "ThirdParty/tinybvh/tiny_bvh.h"

using namespace glm;
static tinybvh::BVH GCpuBvh;

void FCPUAccelerationStructure::InitBVH(Assets::Scene& scene)
{
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

    fmt::print("build bvh takes: {:.0f}ms\n", elapsed);

    UpdateBVH(scene);

    ambientCubes.resize( Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z );
    ambientCubesCopy.resize( Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z );
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

#define RAY_PERFACE 16

// Pre-calculated hemisphere samples in the positive Z direction (up)
// 这个需要是一个四棱锥范围内的射线，而不能是半球
const glm::vec3 hemisphereVectors32[32] = {
    glm::vec3(0.06380871270368209, -0.16411674977923174, 0.984375),
    glm::vec3(-0.2713456981395, 0.13388146427413833, 0.953125),
    glm::vec3(0.3720439325965911, 0.1083041854826636, 0.921875),
    glm::vec3(-0.2360905314110366, -0.38864941831045413, 0.890625),
    glm::vec3(-0.0994527932145428, 0.5015812509422829, 0.859375),
    glm::vec3(0.4518001015172772, -0.3317915801282155, 0.828125),
    glm::vec3(-0.6006110862696151, -0.06524229782152816, 0.796875),
    glm::vec3(0.4246400102205648, 0.4832175711777031, 0.765625),
    glm::vec3(0.014025106917771066, -0.6785990390141627, 0.734375),
    glm::vec3(-0.4910499646725058, 0.5142812135107899, 0.703125),
    glm::vec3(0.7390090634100329, -0.04949331846649647, 0.671875),
    glm::vec3(-0.5995855814426192, -0.47968400004702705, 0.640625),
    glm::vec3(0.12194313936463708, 0.7834487731414841, 0.609375),
    glm::vec3(0.4520746755881747, -0.6792642873483389, 0.578125),
    glm::vec3(-0.8128288753565273, 0.2005915096948101, 0.546875),
    glm::vec3(0.7520560261769773, 0.41053939258723227, 0.515625),
    glm::vec3(-0.28306625928882, -0.8278009134008216, 0.48437499999999994),
    glm::vec3(-0.357091373373129, 0.8168007623879232, 0.4531249999999999),
    glm::vec3(0.8289528112710368, -0.36723115480694823, 0.42187500000000006),
    glm::vec3(-0.8724752793478753, -0.29359665580835004, 0.39062500000000006),
    glm::vec3(0.45112649883473616, 0.8169054360353547, 0.35937499999999994),
    glm::vec3(0.22185204734195418, -0.9182132941017481, 0.328125),
    glm::vec3(-0.792362708282336, 0.5329414347735422, 0.296875),
    glm::vec3(0.9533182286148584, 0.1436235160606669, 0.26562499999999994),
    glm::vec3(-0.6110028678351297, -0.756137457657169, 0.23437499999999994),
    glm::vec3(-0.06066310322190489, 0.9772718261990818, 0.20312499999999992),
    glm::vec3(0.7091634641369421, -0.683773475288631, 0.17187500000000008),
    glm::vec3(-0.9897399665274648, 0.025286518803760413, 0.14062500000000008),
    glm::vec3(0.749854715318357, 0.6524990538612495, 0.1093750000000001),
    glm::vec3(-0.11249484107422018, -0.9905762944401031, 0.0781250000000001),
    glm::vec3(-0.587325250956154, 0.8079924405365998, 0.046875),
    glm::vec3(0.9798239737002217, -0.19925069620281746, 0.01562500000000002)
};
const glm::vec3 hemisphereVectors16[16] = {
    glm::vec3(0.0898831725494359, -0.23118056318048955, 0.96875),
    glm::vec3(-0.3791079112581037, 0.18705114039085075, 0.90625),
    glm::vec3(0.5153445316614019, 0.15001983597741456, 0.84375),
    glm::vec3(-0.3240808043433482, -0.5334979566560387, 0.78125),
    glm::vec3(-0.1352243320429325, 0.6819918016542008, 0.71875),
    glm::vec3(0.6081648613009205, -0.4466222553554984, 0.65625),
    glm::vec3(-0.7999438572571327, -0.08689512492988453, 0.59375),
    glm::vec3(0.559254806831153, 0.6364019944471022, 0.53125),
    glm::vec3(0.018252552906059358, -0.8831422772194816, 0.46875000000000006),
    glm::vec3(-0.6310280835640938, 0.66088160456577, 0.40624999999999994),
    glm::vec3(0.9369622623684265, -0.06275074818231247, 0.34374999999999994),
    glm::vec3(-0.7493392047328667, -0.5994907787033216, 0.2812499999999999),
    glm::vec3(0.1500724796134238, 0.9641715036043528, 0.21875),
    glm::vec3(0.5472431702110503, -0.8222596002220706, 0.15624999999999997),
    glm::vec3(-0.9665972247046685, 0.2385387655984508, 0.09375000000000008),
    glm::vec3(0.8773063928879596, 0.47891223673854594, 0.03125000000000001)
};

const glm::vec3 directions[6] = {
    glm::vec3(0, 0, 1), // PosZ
    glm::vec3(0, 0, -1), // NegZ
    glm::vec3(0, 1, 0), // PosY
    glm::vec3(0, -1, 0), // NegY
    glm::vec3(1, 0, 0), // PosX
    glm::vec3(-1, 0, 0) // NegX
};

#define MAX_ILLUMINANCE 20.f

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
    result.reserve(RAY_PERFACE);

    // Create orthonormal basis
    glm::vec3 up = glm::abs(normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

    // Transform matrix from Z-up to normal-up space
    glm::mat3 transform(tangent, bitangent, normal);

    // Transform each pre-calculated sample
    for (const auto& sample : hemisphereVectors16)
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
    glm::vec3 probePos = glm::vec3(x, y, z) * Assets::CUBE_UNIT + Assets::CUBE_OFFSET;
    Assets::AmbientCube& cube = ambientCubes[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x];

    cube.Info = glm::vec4(1, 0, 0, 0);

    for (int face = 0; face < 6; face++)
    {
    glm::vec3 baseDir = directions[face];

    float occlusion = 0.0f;
    glm::vec4 rayColor = glm::vec4(0, 0, 0, 0);
    std::vector<glm::vec3> hemisphereSamples;
    GetHemisphereSamples(baseDir, hemisphereSamples);

    bool nextCube = false;
    for (int i = 0; i < RAY_PERFACE; i++)
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
                float power = 0.5f;
                // occlude, bounce color, fade by distance
                uint32_t diffuseColor = instContext.mats[context.extinfos[primIdx].matIdx];
                
                rayColor += unpackUnorm4x8(diffuseColor) * power;// * glm::clamp(1.0f - ray.hit.t / 5.0f, 0.0f, 1.0f) * 0.5f;
                occlusion += 1.0f;
            }
            else
            {
                cube.Info.x = 0;
                cube.Info.y = 0;
                nextCube = true;
                break;
            }
        }
        else
        {
            // hit the sky, sky color, with a ibl is better
            float skyBrightness = hemisphereSamples[i].y > 0.0f ? 1.0f : 0.0f;
            glm::vec4 skyColor = glm::vec4(0.6, 0.7, 1.0, 1.0f) * skyBrightness;
            rayColor += skyColor;
        }
    }

    if (nextCube)
    {
        continue;
    }

    occlusion /= RAY_PERFACE;
    rayColor /= RAY_PERFACE;

    // for each light in the scene, ray hit the center, if not occlude, add rayColor
    for( auto& light : lightPos )
    {
        if( !IsInShadow( probePos, light, GCpuBvh) )
        {
            // ndotl + distance attenuation
            glm::vec3 lightDir = glm::normalize(light - probePos);
            float ndotl = glm::clamp(glm::dot(baseDir, lightDir), 0.0f, 1.0f);
            float distance = glm::length(light - probePos);
            float attenuation = 40.0f / (distance * distance);
            rayColor += glm::vec4(0.6f, 0.6f, 0.6f, 1.0f) * ndotl * attenuation;
        }
    }

    float visibility = 1.0f - occlusion;
    glm::vec4 indirectColor = rayColor;//glm::vec4(visibility);
    uint32_t packedColor = packRGB10A2(indirectColor);

    switch (face)
    {
    case 0: cube.PosZ = packedColor;
        break;
    case 1: cube.NegZ = packedColor;
        break;
    case 2: cube.PosY = packedColor;
        break;
    case 3: cube.NegY = packedColor;
        break;
    case 4: cube.PosX = packedColor;
        break;
    case 5: cube.NegX = packedColor;
        break;
    }
    }

    if (!sunDir.empty())
    {
        IsInShadow(probePos, probePos + sunDir[0] * 1000.0f, GCpuBvh) ? cube.Info.y = 0 : cube.Info.y = 1;
    }
    
    ambientCubesCopy[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x] = cube;
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

