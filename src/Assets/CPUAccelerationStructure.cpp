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

#define MAX_ILLUMINANCE 512.f
static const uint FACE_TRACING = 16;
static const float PT_MAX_TRACE_DISTANCE = 1000.f;
static const float FAST_MAX_TRACE_DISTANCE = 10.f;

const vec3 cubeVectors[6] = {
    vec3(0, 1, 0),
    vec3(0, -1, 0),
    vec3(0, 0, 1),
    vec3(0, 0, -1),
    vec3(1, 0, 0),
    vec3(-1, 0, 0),
    };

const vec2 grid4x4[16] = {
    vec2(-0.75, -0.75), vec2(-0.25, -0.75), vec2(0.25, -0.75), vec2(0.75, -0.75),
    vec2(-0.75, -0.25), vec2(-0.25, -0.25), vec2(0.25, -0.25), vec2(0.75, -0.25),
    vec2(-0.75,  0.25), vec2(-0.25,  0.25), vec2(0.25,  0.25), vec2(0.75,  0.25),
    vec2(-0.75,  0.75), vec2(-0.25,  0.75), vec2(0.25,  0.75), vec2(0.75,  0.75)
};

const vec2 grid5x5[25] = {
    vec2(-0.8, -0.8), vec2(-0.4, -0.8), vec2(0.0, -0.8), vec2(0.4, -0.8), vec2(0.8, -0.8),
    vec2(-0.8, -0.4), vec2(-0.4, -0.4), vec2(0.0, -0.4), vec2(0.4, -0.4), vec2(0.8, -0.4),
    vec2(-0.8,  0.0), vec2(-0.4,  0.0), vec2(0.0,  0.0), vec2(0.4,  0.0), vec2(0.8,  0.0),
    vec2(-0.8,  0.4), vec2(-0.4,  0.4), vec2(0.0,  0.4), vec2(0.4,  0.4), vec2(0.8,  0.4),
    vec2(-0.8,  0.8), vec2(-0.4,  0.8), vec2(0.0,  0.8), vec2(0.4,  0.8), vec2(0.8,  0.8)
};

vec3 EvaluateSH(float SHCoefficients[3][9], vec3 normal, float rotate) {
    // Apply rotation around Y-axis (0 to 2 maps to 0 to 360 degrees)
    float angle = rotate * 3.14159265358979323846f;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
	
    // Rotate the normal vector around Y-axis
    vec3 rotatedNormal = vec3(
        normal.x * cosAngle + normal.z * sinAngle,
        normal.y,
        -normal.x * sinAngle + normal.z * cosAngle
    );
	
    // SH basis function evaluation
    const float SH_C0 = 0.282095f;
    const float SH_C1 = 0.488603f;
    const float SH_C2 = 1.092548f;
    const float SH_C3 = 0.315392f;
    const float SH_C4 = 0.546274f;
	
    float basis[9];
    basis[0] = SH_C0;
    basis[1] = -SH_C1 * rotatedNormal.y;
    basis[2] = SH_C1 * rotatedNormal.z;
    basis[3] = -SH_C1 * rotatedNormal.x;
    basis[4] = SH_C2 * rotatedNormal.x * rotatedNormal.y;
    basis[5] = -SH_C2 * rotatedNormal.y * rotatedNormal.z;
    basis[6] = SH_C3 * (3.f * rotatedNormal.y * rotatedNormal.y - 1.0f);
    basis[7] = -SH_C2 * rotatedNormal.x * rotatedNormal.z;
    basis[8] = SH_C4 * (rotatedNormal.x * rotatedNormal.x - rotatedNormal.z * rotatedNormal.z);
	
    vec3 color = vec3(0.0);
    for (int i = 0; i < 9; ++i) {
        color.r += SHCoefficients[0][i] * basis[i];
        color.g += SHCoefficients[1][i] * basis[i];
        color.b += SHCoefficients[2][i] * basis[i];
    }
	
    return color;
}

vec4 SampleIBLRough(uint skyIdx, vec3 direction, float rotate)
{
    vec3 rayColor = EvaluateSH(HDRSHs[skyIdx].coefficients, direction, 1.0f - rotate);
    return vec4(rayColor, 1.0);
}

uint packRGB10A2(vec4 color) {
    vec4 clamped = clamp( color / MAX_ILLUMINANCE, vec4(0.0f), vec4(1.0f) );
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

uint PackColor(vec4 source) {
    return packRGB10A2(source);
}

vec4 UnpackColor(uint packed) {
    return unpackRGB10A2(packed);
}

uint LerpPackedColor(uint c0, uint c1, float t) {
    vec4 color0 = UnpackColor(c0);
    vec4 color1 = UnpackColor(c1);
    return PackColor(mix(color0, color1, t));
}

uint LerpPackedColorAlt(uint c0, vec4 c1, float t) {
    vec4 color0 = UnpackColor(c0);
    vec4 color1 = c1;
    return PackColor(mix(color0, color1, t));
}

vec4 sampleAmbientCubeHL2_DI(AmbientCube cube, vec3 normal) {
    vec4 color = vec4(0.0);
    float sum = 0.0;
    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);
    sum = wx + wnx + wy + wny + wz + wnz;
    color += wx *   UnpackColor(cube.PosX_D);
    color += wnx *  UnpackColor(cube.NegX_D);
    color += wy *   UnpackColor(cube.PosY_D);
    color += wny *  UnpackColor(cube.NegY_D);
    color += wz *   UnpackColor(cube.PosZ_D);
    color += wnz *  UnpackColor(cube.NegZ_D);
    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    //color.w = unpackHalf2x16(cube.Lighting).x;
    return color;
}

LightObject FetchLight(uint lightIdx, vec4& lightPower)
{
    auto& Lights = NextEngine::GetInstance()->GetScene().Lights();
    auto& materials = NextEngine::GetInstance()->GetScene().Materials();
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

AmbientCube& FetchCube(ivec3 probePos)
{
    int idx = probePos.y * CUBE_SIZE_XY * CUBE_SIZE_XY + probePos.z * CUBE_SIZE_XY + probePos.x;
    return (*TLSContext.Cubes)[idx];
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

vec4 FetchDirectLight(vec3 hitPos, vec3 normal)
{
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
        //if (cube.Active != 1) continue;

        float wx = offset.x == 0 ? (1.0f - frac.x) : frac.x;
        float wy = offset.y == 0 ? (1.0f - frac.y) : frac.y;
        float wz = offset.z == 0 ? (1.0f - frac.z) : frac.z;
        float weight = wx * wy * wz;

        vec4 sampleColor = sampleAmbientCubeHL2_DI(cube, normal);
        result += sampleColor * weight;
        totalWeight += weight;
    }

    vec4 indirectColor = totalWeight > 0.0 ? result / totalWeight : vec4(0.05f);
    return indirectColor;
}

bool TraceSegment(vec3 origin, vec3 lightPos)
{
    vec3 direction = lightPos - origin;
    float length = glm::length(direction) - 0.02f;
    tinybvh::Ray shadowRay(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(direction.x, direction.y, direction.z), length);
    return GCpuBvh.IsOccluded(shadowRay);
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
        OutInstanceId = ray.hit.inst;
        return true;
    }
    
    return false;
}

#define float2 vec2
#define float3 vec3
#define float4 vec4

bool InsideGeometry( float3& origin, float3 rayDir, uint& OutMaterialId)
{
    // 求交测试
    vec3 OutNormal;
    uint OutInstanceId;
    float OutRayDist;
    uint TempMaterialId;

    if (TraceRay(origin, rayDir, CUBE_UNIT, OutNormal, TempMaterialId, OutRayDist, OutInstanceId))
    {
        vec3 hitPos = origin + rayDir * OutRayDist;
        FMaterial hitMaterial = FetchMaterial(TempMaterialId);
        // 命中反面，识别为固体，并将lightprobe推出体外
        if (dot(OutNormal, rayDir) > 0.0)
        {
            float hitRayDist = OutRayDist + 0.05f;
            origin += rayDir * hitRayDist;
            OutMaterialId = TempMaterialId;
            return true;
        }
        // 命中光源，不论正反，识别为固体
        if (hitMaterial.gpuMaterial_.MaterialModel == Material::Enum::DiffuseLight)
        {
            OutMaterialId = TempMaterialId;
            return true;
        }
    }
    //OutMaterialId = 0;
    return false;
}

    bool FaceTask(float3 origin, float3 basis, uint iterate, uint& DirectLight, uint& IndirectLight)
    {
        auto& Camera = NextEngine::GetInstance()->GetUniformBufferObject();
    
        // 输出结果
        float4 directColor = float4(0.0);
        float4 bounceColor = float4(0.0);

        // 抖动
        const float2 offset = grid5x5[iterate % 25] * 0.25f;

        // 天光直接光照和反弹
        for (uint i = 0; i < FACE_TRACING; i++)
        {
            float3 hemiVec = normalize(float3(grid4x4[i] + offset, 1.0));
            float3 rayDir = AlignWithNormal(hemiVec, basis);

            vec3 OutNormal;
            uint OutInstanceId;
            uint OutMaterialId;
            float OutRayDist;
            if (TraceRay(origin, rayDir, FAST_MAX_TRACE_DISTANCE, OutNormal, OutMaterialId, OutRayDist, OutInstanceId))
            {
                vec3 hitPos = origin + rayDir * OutRayDist;
                FMaterial hitMaterial = FetchMaterial(OutMaterialId);
                float4 outAlbedo = hitMaterial.gpuMaterial_.Diffuse;
                bounceColor += outAlbedo * FetchDirectLight(hitPos, OutNormal);
            }
            else
            {
                directColor += SampleIBLRough(Camera.SkyIdx, rayDir, Camera.SkyRotation) * (Camera.HasSky ? Camera.SkyIntensity : 0.0f);
            }
        }
        directColor = directColor / float(FACE_TRACING);
        bounceColor = bounceColor / float(FACE_TRACING);

        // 参数光源，目前只取了一盏光源
        if (Camera.LightCount > 0)
        {
            float4 lightPower = float4(0.0);
            LightObject light = FetchLight(0, lightPower);
            float3 lightPos = mix(vec3(light.p1.x, light.p1.y, light.p1.z), vec3(light.p3.x, light.p3.y, light.p3.z), 0.5f);
            float lightAtten = TraceSegment(origin, lightPos) ? 0.0f : 1.0f;
            float3 lightDir = normalize(lightPos - origin);
            float ndotl = clamp(dot(basis, lightDir), 0.0f, 1.0f);
            float distance = length(lightPos - origin);
            float attenuation = ndotl * light.normal_area.w / (distance * distance * 3.14159f);
            directColor += lightPower * attenuation * lightAtten;
        }

        // 太阳光
        if (Camera.HasSun)
        {
            float3 sunDir = float3(Camera.SunDirection.x, Camera.SunDirection.y, Camera.SunDirection.z);
            float sunAtten = TraceSegment(origin, origin + sunDir * 50.0f) ? 0.0f : 1.0f;
            float ndotl = clamp(dot(basis, sunDir), 0.0f, 1.0f);
            directColor += Camera.SunColor * sunAtten * ndotl * (Camera.HasSun ? 1.0f : 0.0f) * 0.25f;
        }

        // 累积，gpu直接只保留4帧
        DirectLight = LerpPackedColorAlt(DirectLight, directColor, 0.25);
        IndirectLight = LerpPackedColorAlt(IndirectLight, bounceColor, 0.125);
        return false;
    }

void VoxelizeCube(VoxelData& Cube, float3 origin)
{
    // just write matid and solid status
    Cube.matId = 0;

    InsideGeometry(origin, float3(0, 1, 0), Cube.matId);
    InsideGeometry(origin, float3(0, -1, 0), Cube.matId);
    InsideGeometry(origin, float3(1, 0, 0), Cube.matId);
    InsideGeometry(origin, float3(-1, 0, 0), Cube.matId);
    InsideGeometry(origin, float3(0, 0, 1), Cube.matId);
    InsideGeometry(origin, float3(0, 0, -1), Cube.matId);
}
void RenderCube(AmbientCube& Cube, float3 origin)
{
    // uint iterate = Cube.ExtInfo2;
    // Cube.ExtInfo2 = Cube.ExtInfo2 + 1;
    // bool Solid = false;
    //
    // Solid = Solid || InsideGeometry(origin, float3(0, 1, 0), Cube.ExtInfo1);
    // Solid = Solid || InsideGeometry(origin, float3(0, -1, 0), Cube.ExtInfo1);
    // Solid = Solid || InsideGeometry(origin, float3(1, 0, 0), Cube.ExtInfo1);
    // Solid = Solid || InsideGeometry(origin, float3(-1, 0, 0), Cube.ExtInfo1);
    // Solid = Solid || InsideGeometry(origin, float3(0, 0, 1), Cube.ExtInfo1);
    // Solid = Solid || InsideGeometry(origin, float3(0, 0, -1), Cube.ExtInfo1);
    //
    // FaceTask(origin, float3(0, 1, 0), iterate, Cube.PosY_D, Cube.PosY);
    // FaceTask(origin, float3(0, -1, 0), iterate, Cube.NegY_D, Cube.NegY);
    // FaceTask(origin, float3(1, 0, 0), iterate, Cube.PosX_D, Cube.PosX);
    // FaceTask(origin, float3(-1, 0, 0), iterate, Cube.NegX_D, Cube.NegX);
    // FaceTask(origin, float3(0, 0, 1), iterate, Cube.PosZ_D, Cube.PosZ);
    // FaceTask(origin, float3(0, 0, -1), iterate, Cube.NegZ_D, Cube.NegZ);
    //
    // Cube.Active = Solid ? 0 : 1;
}

#undef float2
#undef float3
#undef float4

void FCPUProbeBaker::Init(float unit_size, vec3 offset)
{
    UNIT_SIZE = unit_size;
    CUBE_OFFSET = offset;
    ambientCubes.resize( CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z );
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

    tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z));
    GCpuBvh.Intersect(ray);
    
    if (ray.hit.t < 100000.f)
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
        Result.InstanceId = ray.hit.inst;
    }

    return Result;
}

void FCPUProbeBaker::ProcessCube(int x, int y, int z, ECubeProcType procType)
{
    auto& ubo = NextEngine::GetInstance()->GetUniformBufferObject();
    vec3 probePos = vec3(x, y, z) * UNIT_SIZE + CUBE_OFFSET;
    uint32_t addressIdx = y * CUBE_SIZE_XY * CUBE_SIZE_XY + z * CUBE_SIZE_XY + x;
    AmbientCube& cube = ambientCubes[addressIdx];
    VoxelData& voxel = voxels[addressIdx];

    TLSContext.Cubes = &ambientCubes;
    TLSContext.CUBE_UNIT = UNIT_SIZE;
    TLSContext.CUBE_OFFSET = CUBE_OFFSET;
    
    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
        case ECubeProcType::ECPT_Fence:
            break;
        case ECubeProcType::ECPT_Iterate:
            RenderCube(cube, probePos);
            break;
        case ECubeProcType::ECPT_Voxelize:
            VoxelizeCube(voxel, probePos);
            break;
    }
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& GPUMemory, Vulkan::DeviceMemory& VoxelGPUMemory)
{
    {
        AmbientCube* data = reinterpret_cast<AmbientCube*>(GPUMemory.Map(0, sizeof(AmbientCube) * ambientCubes.size()));
        std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(AmbientCube));
    }

    {
        VoxelData* data = reinterpret_cast<VoxelData*>(VoxelGPUMemory.Map(0, sizeof(VoxelData) * voxels.size()));
        std::memcpy(data, voxels.data(), voxels.size() * sizeof(VoxelData));
    }

    GPUMemory.Unmap();
    VoxelGPUMemory.Unmap();
}

void FCPUAccelerationStructure::AsyncProcessFull()
{    
    // clean
    while (!needUpdateGroups.empty())
        needUpdateGroups.pop();
    lastBatchTasks.clear();
    TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    probeBaker.ClearAmbientCubes();

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
        probeBaker.UploadGPU(*GPUMemory, *VoxelGPUMemory);
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
            needUpdateGroups.push({point, ECubeProcType::ECPT_Iterate, EBakerType::EBT_Probe});
        }
    }
}

void FCPUProbeBaker::ClearAmbientCubes()
{
    for(auto& cube : ambientCubes)
    {
        cube = {};
    }

    for(auto& voxel : voxels)
    {
        voxel = {};
    }
}

void FCPUPageIndex::Init()
{
    pageIndex.resize(Assets::PAGE_SIZE * Assets::PAGE_SIZE);
}

void FCPUPageIndex::UpdateData(FCPUProbeBaker& baker)
{
    // 粗暴实现，先全部page置空
    for (auto& page : pageIndex)
    {
        page = Assets::PageIndex();
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
    glm::vec3 relativePos = worldpos - Assets::PAGE_OFFSET;

    // 计算页面索引，假设每个page对应PAGE_UNIT的世界空间距离
    // 使用xz平面进行映射
    int pageX = static_cast<int>(relativePos.x / Assets::PAGE_SIZE);
    int pageZ = static_cast<int>(relativePos.z / Assets::PAGE_SIZE);

    // 限制在有效范围内
    pageX = glm::clamp(pageX, 0, Assets::PAGE_SIZE - 1);
    pageZ = glm::clamp(pageZ, 0, Assets::PAGE_SIZE - 1);

    // 计算一维索引
    int index = pageZ * Assets::PAGE_SIZE + pageX;

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
