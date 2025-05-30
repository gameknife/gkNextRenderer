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

thread_local FCpuBakeContext TLSContext;
thread_local glm::uvec4 RandomSeed(0);

Assets::SphericalHarmonics HDRSHs[100];

using namespace Assets;

#include "../assets/shaders/common/SampleIBL.glsl"
#include "../assets/shaders/common/AmbientCubeCommon.glsl"

//=========================
// Function Implement for Hybrid CPU / GPU Code
LightObject FetchLight(uint lightIdx, vec4& lightPower)
{
    auto Lights = NextEngine::GetInstance()->GetScene().Lights();
    auto materials = NextEngine::GetInstance()->GetScene().Materials();
    lightPower = materials[Lights[lightIdx].lightMatIdx].gpuMaterial_.Diffuse;
    return Lights[lightIdx];
}

vec3 AlignWithNormal(vec3 ray, vec3 normal)
{
    vec3 up = abs(normal.y) < 0.999f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = normalize(cross(normal, tangent));
    mat3 transform(tangent, bitangent, normal);
    return transform * ray;
}

float RandomFloat(uvec4& v)
{
    v.x = (v.x ^ 61) ^ (v.x >> 16);
    v.x = v.x + (v.x << 3);
    v.x = v.x ^ (v.x >> 4);
    v.x = v.x * 0x27d4eb2d;
    v.x = v.x ^ (v.x >> 15);
    return float(v.x) / float(0xFFFFFFFF);
}

int TracingOccludeFunction(vec3 origin, vec3 lightPos)
{
    vec3 direction = lightPos - origin;
    float length = glm::length(direction) - 0.02f;
    tinybvh::Ray shadowRay(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(direction.x, direction.y, direction.z), length);
    return GCpuBvh.IsOccluded(shadowRay) ? 0 : 1;
}

bool TracingFunction(vec3 origin, vec3 rayDir, vec3& OutNormal, uint& OutMaterialId, float& OutRayDist, uint& OutInstanceId )
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), 11.f);
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
    
    return false;
}

AmbientCube& FetchCube(ivec3 probePos)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY + probePos.z * CUBE_SIZE_XY + probePos.x;
    return (*TLSContext.Cubes)[idx];
}

uint FetchMaterialId(uint MaterialIdx, uint InstanceId)
{
    return (*GbvhTLASContexts)[InstanceId].matIdxs[MaterialIdx];
}

vec4 FetchDirectLight(vec3 hitPos, vec3 normal, uint OutMaterialId, uint OutInstanceId)
{
    uint32_t diffuseColor = (*GbvhTLASContexts)[OutInstanceId].mats[OutMaterialId];
    vec4 albedo = unpackUnorm4x8(diffuseColor);
    
    vec3 pos = (hitPos - TLSContext.CUBE_OFFSET) / TLSContext.CUBE_UNIT;
    if (pos.x < 0 || pos.y < 0 || pos.z < 0 ||
    pos.x > CUBE_SIZE_XY - 1 || pos.y > CUBE_SIZE_Z - 1 || pos.z > CUBE_SIZE_XY - 1) {
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
//=========================

#include "../assets/shaders/common/AmbientCubeAlgo.glsl"


void FCPUProbeBaker::Init(float unit_size, vec3 offset)
{
    UNIT_SIZE = unit_size;
    CUBE_OFFSET = offset;
    ambientCubes.resize( CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z );
    ambientCubes_Copy.resize( CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z );
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
    farProbeBaker.Init( CUBE_UNIT_FAR, CUBE_OFFSET_FAR );
    
    UpdateBVH(scene);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    bvhInstanceList.clear();
    bvhTLASContexts.clear();

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

        bvhInstanceList.push_back(instance);
        FCPUTLASInstanceInfo info;
        for ( int i = 0; i < node->Materials().size(); ++i )
        {
            uint32_t matId = node->Materials()[i];
            FMaterial& mat = scene.Materials()[matId];
            info.mats[i] = packUnorm4x8( mat.gpuMaterial_.Diffuse );
            info.matIdxs[i] = matId;
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
}

RayCastResult FCPUAccelerationStructure::RayCastInCPU(vec3 rayOrigin, vec3 rayDir)
{
    RayCastResult Result;

    // tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z));
    // GCpuBvh.Intersect(ray);
    //
    // if (ray.hit.t < 100000.f)
    // {
    //     vec3 hitPos = rayOrigin + rayDir * ray.hit.t;
    //
    //     Result.HitPoint = vec4(hitPos, 0);
    //     Result.Normal = vec4(GExtInfos[ray.hit.prim].normal, 0);
    //     Result.Hitted = true;
    //     Result.InstanceId = GExtInfos[ray.hit.prim].instanceId;
    // }

    return Result;
}

void FCPUProbeBaker::ProcessCube(int x, int y, int z, ECubeProcType procType)
{
    auto& ubo = NextEngine::GetInstance()->GetUniformBufferObject();
    vec3 probePos = vec3(x, y, z) * UNIT_SIZE + CUBE_OFFSET;
    uint32_t addressIdx = y * CUBE_SIZE_XY * CUBE_SIZE_XY + z * CUBE_SIZE_XY + x;
    AmbientCube& cube = ambientCubes[addressIdx];

    TLSContext.Cubes = &ambientCubes;
    TLSContext.CUBE_UNIT = UNIT_SIZE;
    TLSContext.CUBE_OFFSET = CUBE_OFFSET;
    
    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
        case ECubeProcType::ECPT_Fence:
            break;
        case ECubeProcType::ECPT_Iterate:
            {
                cube.Active = 1;
                cube.Lighting = 0;
                cube.ExtInfo1 = 0;
                cube.ExtInfo2 = cube.ExtInfo2 + 1;
                
                vec4 bounceColor(0);
                vec4 skyColor(0);
                uint matId = 0;

                vec3 offset = vec3(0);
                bool Inside = IsInside( probePos, offset, matId);

                if (Inside)
                {
                    probePos = probePos + offset;
                    cube.Active = 0;
                }
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
                // 正Y方向
                cube.PosY_D = packRGB10A2( TraceOcclusion(  cube.ExtInfo2, probePos, vec3(0,1,0), cube.Active, matId, bounceColor, skyColor, ubo) );
                cube.PosY = LerpPackedColorAlt( cube.PosY, bounceColor, 1.0f / cube.ExtInfo2 );
                cube.PosY_S = LerpPackedColorAlt( cube.PosY_S, skyColor, 1.0f / cube.ExtInfo2 );
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
                // 负Y方向
                cube.NegY_D = packRGB10A2( TraceOcclusion(  cube.ExtInfo2, probePos, vec3(0,-1,0), cube.Active, matId, bounceColor, skyColor, ubo) );
                cube.NegY = LerpPackedColorAlt( cube.NegY, bounceColor, 1.0f / cube.ExtInfo2 );
                cube.NegY_S = LerpPackedColorAlt( cube.NegY_S, skyColor, 1.0f / cube.ExtInfo2 );
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
                
                // 正X方向
                cube.PosX_D = packRGB10A2( TraceOcclusion(  cube.ExtInfo2, probePos, vec3(1,0,0), cube.Active, matId, bounceColor, skyColor, ubo) );
                cube.PosX = LerpPackedColorAlt( cube.PosX, bounceColor, 1.0f / cube.ExtInfo2 );
                cube.PosX_S = LerpPackedColorAlt( cube.PosX_S, skyColor, 1.0f / cube.ExtInfo2 );
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
                
                // 负X方向
                cube.NegX_D = packRGB10A2( TraceOcclusion(  cube.ExtInfo2, probePos, vec3(-1,0,0), cube.Active, matId, bounceColor, skyColor, ubo) );
                cube.NegX = LerpPackedColorAlt( cube.NegX, bounceColor, 1.0f / cube.ExtInfo2 );
                cube.NegX_S = LerpPackedColorAlt( cube.NegX_S, skyColor, 1.0f / cube.ExtInfo2 );
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
                
                // 正Z方向
                cube.PosZ_D = packRGB10A2( TraceOcclusion(  cube.ExtInfo2, probePos, vec3(0,0,1), cube.Active, matId, bounceColor, skyColor, ubo) );
                cube.PosZ = LerpPackedColorAlt( cube.PosZ, bounceColor, 1.0f / cube.ExtInfo2 );
                cube.PosZ_S = LerpPackedColorAlt( cube.PosZ_S, skyColor, 1.0f / cube.ExtInfo2 );
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
                
                // 负Z方向
                cube.NegZ_D = packRGB10A2( TraceOcclusion(  cube.ExtInfo2, probePos, vec3(0,0,-1), cube.Active, matId, bounceColor, skyColor, ubo) );
                cube.NegZ = LerpPackedColorAlt( cube.NegZ, bounceColor, 1.0f / cube.ExtInfo2 );
                cube.NegZ_S = LerpPackedColorAlt( cube.NegZ_S, skyColor, 1.0f / cube.ExtInfo2 );
                cube.ExtInfo1 = matId;
                if (cube.Active == 0) return;
            }
            break;
        case ECubeProcType::ECPT_Copy:
            {
                ambientCubes_Copy[addressIdx] = ambientCubes[addressIdx];
            }
            break;
        case ECubeProcType::ECPT_Blur:
        {
            uint32_t centerIdx = y * CUBE_SIZE_XY * CUBE_SIZE_XY + z * CUBE_SIZE_XY + x;
            AmbientCube& centerCube = ambientCubes[centerIdx];
            centerCube.ExtInfo3 = 0;

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
                            nx >= CUBE_SIZE_XY ||
                            ny >= CUBE_SIZE_Z ||
                            nz >= CUBE_SIZE_XY) {
                            continue;
                        }

                        uint32_t neighborIdx = ny * CUBE_SIZE_XY * CUBE_SIZE_XY + nz * CUBE_SIZE_XY + nx;
                        AmbientCube& neighborCube = ambientCubes_Copy[neighborIdx];

                        // 如果邻居立方体不活跃，跳过
                        if (neighborCube.Active != 1) continue;

                        // 计算权重 (距离越远权重越小)
                        float dist = length(vec3(dx, dy, dz));
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

                centerCube.ExtInfo3 = 1;
            }
        }
        break;
    }
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& GPUMemory)
{
    AmbientCube* data = reinterpret_cast<AmbientCube*>(GPUMemory.Map(0, sizeof(AmbientCube) * ambientCubes.size()));
    std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(AmbientCube));
    GPUMemory.Unmap();
}

void FCPUAccelerationStructure::AsyncProcessFull()
{    
    // clean
    while (!needUpdateGroups.empty())
        needUpdateGroups.pop();
    lastBatchTasks.clear();
    TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    probeBaker.ClearAmbientCubes();
    farProbeBaker.ClearAmbientCubes();
    
    const int groupSize = 16;
    const int lengthX = CUBE_SIZE_XY / groupSize;
    const int lengthZ = CUBE_SIZE_XY / groupSize;

    // far probe gen
    for (int x = 0; x < lengthX; x++)
        for (int z = 0; z < lengthZ; z++)
            needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Iterate, EBakerType::EBT_FarProbe});
    
    // 2 pass near probe iterate
    for(int pass = 0; pass < 4; ++pass)
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
            needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Iterate, EBakerType::EBT_Probe});
        // add fence
        needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe});
    }

    // copy for blur
    for (int x = 0; x < lengthX; x++)
        for (int z = 0; z < lengthZ; z++)
            needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Copy, EBakerType::EBT_Probe});
    needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe});

    // blur pass
    for (int x = 0; x < lengthX; x++)
        for (int z = 0; z < lengthZ; z++)
            needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Blur, EBakerType::EBT_Probe});
    needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe});
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
                [this, actualX, actualZ, groupSize, procType, bakerType](ResTask& task)
            {
                for (int z = actualZ; z < actualZ + groupSize; z++)
                    for (int y = 0; y < CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                        {
                            bakerType == EBakerType::EBT_Probe ? probeBaker.ProcessCube(x, y, z, procType) : farProbeBaker.ProcessCube(x, y, z, procType);
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

void FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* GPUMemory, Vulkan::DeviceMemory* FarGPUMemory)
{
    if (needFlush)
    {
        // Upload to GPU, now entire range, optimize to partial upload later
        probeBaker.UploadGPU(*GPUMemory);
        farProbeBaker.UploadGPU(*FarGPUMemory);
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
            needUpdateGroups.push({point, ECubeProcType::ECPT_Iterate, EBakerType::EBT_Probe});
        }
    }
}

void FCPUProbeBaker::ClearAmbientCubes()
{
    for(auto& cube : ambientCubes)
    {
        cube = {};
        cube.Active = 1;
    }
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