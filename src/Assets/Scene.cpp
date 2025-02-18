#include "Scene.hpp"
#include "Model.hpp"
#include "Options.hpp"
#include "Sphere.hpp"
#include "Vulkan/BufferUtil.hpp"

#define TINYBVH_IMPLEMENTATION
#include <chrono>
#include <atomic>

#include "Runtime/TaskCoordinator.hpp"
#include "ThirdParty/tinybvh/tiny_bvh.h"

struct VertInfo
{
    glm::vec3 normal;
    uint32_t instanceId;
};

static tinybvh::BVH GCpuBvh;
// represent every triangles verts one by one, triangle count is size / 3
static std::vector<tinybvh::bvhvec4> GTriangles;
static std::vector<VertInfo> GExtInfos;

namespace Assets
{
    Scene::Scene(Vulkan::CommandPool& commandPool,
                 bool supportRayTracing)
    {
        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        RebuildMeshBuffer(commandPool, supportRayTracing);

        // 动态更新的场景结构，每帧更新
        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Nodes", flags, sizeof(NodeProxy) * 65535, nodeMatrixBuffer_, nodeMatrixBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_,
                                                      indirectDrawBufferMemory_); // support 65535 nodes


        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Materials", flags, sizeof(Material) * 4096, materialBuffer_, materialBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "AmbientCubes", flags, Assets::CUBE_SIZE * Assets::CUBE_SIZE * Assets::CUBE_SIZE * sizeof(Assets::AmbientCube), ambientCubeBuffer_,
                                                    ambientCubeBufferMemory_);
    }

    Scene::~Scene()
    {
        offsetBuffer_.reset();
        offsetBufferMemory_.reset(); // release memory after bound buffer has been destroyed

        indexBuffer_.reset();
        indexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        vertexBuffer_.reset();
        vertexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        lightBuffer_.reset();
        lightBufferMemory_.reset();

        indirectDrawBuffer_.reset();
        indirectDrawBufferMemory_.reset();
        nodeMatrixBuffer_.reset();
        nodeMatrixBufferMemory_.reset();
        materialBuffer_.reset();
        materialBufferMemory_.reset();

        ambientCubeBuffer_.reset();
        ambientCubeBufferMemory_.reset();
    }

    void Scene::Reload(std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models, std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                       std::vector<AnimationTrack>& tracks)
    {
        nodes_ = std::move(nodes);
        models_ = std::move(models);
        materials_ = std::move(materials);
        lights_ = std::move(lights);
        tracks_ = std::move(tracks);
    }

    void Scene::RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        // Rebuild the cpu bvh

        // tier1: simple flatten whole scene
        RebuildBVH();

        // tier2: top level, the aabb, bottom level, the triangles in local sapace, with 2 ray relay

        // 重建universe mesh buffer, 这个可以比较静态
        std::vector<GPUVertex> vertices;
        std::vector<uint32_t> indices;

        offsets_.clear();
        for (auto& model : models_)
        {
            // Remember the index, vertex offsets.
            const auto indexOffset = static_cast<uint32_t>(indices.size());
            const auto vertexOffset = static_cast<uint32_t>(vertices.size());

            offsets_.emplace_back(indexOffset, vertexOffset);

            // Copy model data one after the other.

            // cpu vertex to gpu vertex
            for (auto& vertex : model.CPUVertices())
            {
                vertices.push_back(MakeVertex(vertex));
            }
            //vertices.insert(vertices.end(), model.Vertices().begin(), model.Vertices().end());
            indices.insert(indices.end(), model.CPUIndices().begin(), model.CPUIndices().end());

            model.FreeMemory();
        }

        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices, vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, indices, indexBuffer_, indexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_, offsetBufferMemory_);

        // 材质和灯光也应考虑更新
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights_, lightBuffer_, lightBufferMemory_);

        // 一些数据
        lightCount_ = static_cast<uint32_t>(lights_.size());
        indicesCount_ = static_cast<uint32_t>(indices.size());
        verticeCount_ = static_cast<uint32_t>(vertices.size());

        UpdateMaterial();
        MarkDirty();

        GenerateAmbientCubeCPU();
    }

    void Scene::RebuildBVH()
    {
        GTriangles.clear();
        GExtInfos.clear();

        for (auto& node : nodes_)
        {
            node->RecalcTransform(true);

            uint32_t modelId = node->GetModel();
            if (modelId == -1) continue;
            Model& model = models_[modelId];

            for (size_t i = 0; i < model.CPUIndices().size(); i += 3)
            {
                // Get the three vertices of the triangle
                Vertex& v0 = model.CPUVertices()[model.CPUIndices()[i]];
                Vertex& v1 = model.CPUVertices()[model.CPUIndices()[i + 1]];
                Vertex& v2 = model.CPUVertices()[model.CPUIndices()[i + 2]];
            
                // Transform vertices to world space
                glm::vec4 wpos_v0 = node->WorldTransform() * glm::vec4(v0.Position, 1);
                glm::vec4 wpos_v1 = node->WorldTransform() * glm::vec4(v1.Position, 1);
                glm::vec4 wpos_v2 = node->WorldTransform() * glm::vec4(v2.Position, 1);
            
                // Perspective divide
                wpos_v0 /= wpos_v0.w;
                wpos_v1 /= wpos_v1.w;
                wpos_v2 /= wpos_v2.w;
            
                // Calculate face normal
                glm::vec3 edge1 = glm::vec3(wpos_v1) - glm::vec3(wpos_v0);
                glm::vec3 edge2 = glm::vec3(wpos_v2) - glm::vec3(wpos_v0);
                glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

                
                // Add triangle vertices to BVH
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v0.x, wpos_v0.y, wpos_v0.z, 0));
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v1.x, wpos_v1.y, wpos_v1.z, 0));
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v2.x, wpos_v2.y, wpos_v2.z, 0));
            
                // Store additional triangle information
                GExtInfos.push_back({normal, node->GetInstanceId()});
            }
        }

        if (GTriangles.size() >= 3)
        {
            GCpuBvh.Build(GTriangles.data(), static_cast<int>(GTriangles.size()) / 3);
            GCpuBvh.Convert(tinybvh::BVH::WALD_32BYTE, tinybvh::BVH::ALT_SOA, true);
        }
    }

    void Scene::PlayAllTracks()
    {
        for (auto& track : tracks_)
        {
            track.Play();
        }
    }

    Assets::RayCastResult Scene::RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir)
    {
        Assets::RayCastResult Result;

        tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z));
        GCpuBvh.Intersect(ray);

        if (ray.hit.t < 100000.f)
        {
            glm::vec3 hitPos = rayOrigin + rayDir * ray.hit.t;

            Result.HitPoint = glm::vec4(hitPos, 0);
            Result.Normal = glm::vec4(GExtInfos[ray.hit.prim].normal, 0);
            Result.Hitted = true;
            Result.InstanceId = GExtInfos[ray.hit.prim].instanceId;
        }

        return Result;
    }

#define CUBE_SIZE 100
#define RAY_PERFACE 64
    
    inline void fastCopyVec3( tinybvh::bvhvec3& target, glm::vec3& source )
    {
        memcpy( &target, &source, sizeof( glm::vec3 ) );
    }
    inline void fastCopyVec3( tinybvh::bvhvec3& target, tinybvh::bvhvec3& source )
    {
        memcpy( &target, &source, sizeof( tinybvh::bvhvec3 ) );
    }

    // Pre-calculated hemisphere samples in the positive Z direction (up)
 
const glm::vec3 hemisphereVectors[64] = {
    glm::vec3(0.045208304261847965, -0.11627628334926335, 0.9921875),
    glm::vec3(-0.1930181724004224, 0.09523480832631948, 0.9765625),
    glm::vec3(0.26573486325149154, 0.07735698770286432, 0.9609375),
    glm::vec3(-0.16933844306146292, -0.2787629262388916, 0.9453125),
    glm::vec3(-0.07164105709419565, 0.3613152519368318, 0.9296875),
    glm::vec3(0.32689361646125864, -0.24006313671746757, 0.9140625),
    glm::vec3(-0.43653348768112604, -0.04741911773432952, 0.8984375),
    glm::vec3(0.3100703328917848, 0.3528434192444685, 0.8828125),
    glm::vec3(0.010289959042319866, -0.49787544284354635, 0.8671875),
    glm::vec3(-0.3620400469186113, 0.3791679219302705, 0.8515625),
    glm::vec3(0.5475979471417058, -0.03667402868433644, 0.8359375),
    glm::vec3(-0.44658537440975626, -0.357279870279656, 0.8203125),
    glm::vec3(0.09130924535014122, 0.5866350220173848, 0.8046875),
    glm::vec3(0.3403589991660056, -0.5114060253658194, 0.7890625),
    glm::vec3(-0.6154098724502003, 0.15187205959154645, 0.7734375),
    glm::vec3(0.5726979016083169, 0.31262969842479865, 0.7578125),
    glm::vec3(-0.21684466362347532, -0.634142024078023, 0.7421875),
    glm::vec3(-0.27523523182276277, 0.6295653268384626, 0.7265625),
    glm::vec3(0.6429854927306586, -0.2848464976643773, 0.7109375),
    glm::vec3(-0.681173899031339, -0.22922182841560512, 0.6953125),
    glm::vec3(0.3545911939487206, 0.6420981136226542, 0.6796875),
    glm::vec3(0.17559566258035023, -0.7267648583804239, 0.6640625),
    glm::vec3(-0.6316786718677515, 0.4248657012529294, 0.6484375),
    glm::vec3(0.7656645419256517, 0.11535228251548411, 0.6328125),
    glm::vec3(-0.494521160904667, -0.6119872639698072, 0.6171875),
    glm::vec3(-0.04949099612998147, 0.7972911638139558, 0.6015625),
    glm::vec3(0.5833561011650366, -0.5624703595099725, 0.5859375),
    glm::vec3(-0.8211598576906518, 0.02097952480916784, 0.5703125),
    glm::vec3(0.6276889668978702, 0.5461944142687971, 0.5546875),
    glm::vec3(-0.095041039683511, -0.8368863852815549, 0.5390625),
    glm::vec3(-0.5009894513222313, 0.6892189443548397, 0.5234375),
    glm::vec3(0.8441896860109167, -0.17166898053096186, 0.5078124999999999),
    glm::vec3(-0.7461731306626566, -0.44830472217102535, 0.4921875),
    glm::vec3(0.24983794281660274, 0.8428933419614358, 0.4765625000000001),
    glm::vec3(0.3887762013663958, -0.7977403627401998, 0.4609375000000001),
    glm::vec3(-0.8329259898180801, 0.32852864841490065, 0.4453125),
    glm::vec3(0.843190694306946, 0.3231069565607033, 0.42968750000000006),
    glm::vec3(-0.40673584817665814, -0.8143206959802536, 0.4140624999999999),
    glm::vec3(-0.2520605302580668, 0.8818804044085411, 0.3984375000000001),
    glm::vec3(0.7872133087567416, -0.483476779545836, 0.38281250000000006),
    glm::vec3(-0.913256571451911, -0.17645332114655893, 0.3671875),
    glm::vec3(0.5577998880146381, 0.7518398057595828, 0.3515625),
    glm::vec3(0.09714616234904466, -0.9368610458518398, 0.3359375000000001),
    glm::vec3(-0.7085326619425887, 0.628793582429321, 0.3203124999999999),
    glm::vec3(0.9523336958114551, 0.01503526407633283, 0.30468750000000006),
    glm::vec3(-0.6955948361299543, -0.6577162724481522, 0.2890625),
    glm::vec3(0.06895733046331357, 0.9594148321602723, 0.27343749999999994),
    glm::vec3(0.5999014736024121, -0.7573974761070995, 0.25781250000000006),
    glm::vec3(-0.957946743150088, 0.1538936390234811, 0.24218749999999997),
    glm::vec3(0.8134600252899379, 0.535679214501593, 0.22656250000000006),
    glm::vec3(-0.2388298553807905, -0.9478742908595742, 0.21093749999999992),
    glm::vec3(-0.46571344035152984, 0.8631129814918161, 0.19531250000000006),
    glm::vec3(0.9292445387423254, -0.3228265626637823, 0.17968749999999994),
    glm::vec3(-0.9057654321487023, -0.3907332568623058, 0.16406249999999994),
    glm::vec3(0.4049589269614279, 0.9022053957209516, 0.14843749999999992),
    glm::vec3(0.3115244524221444, -0.9409109178805575, 0.13281249999999994),
    glm::vec3(-0.8670032766127175, 0.48432675766114935, 0.11718749999999999),
    glm::vec3(0.9681324707648337, 0.22892046139331595, 0.10156249999999989),
    glm::vec3(-0.560064325448335, -0.8239797918965318, 0.08593749999999999),
    glm::vec3(-0.14379285760812155, 0.9871067654740497, 0.07031250000000003),
    glm::vec3(0.7735674920975367, -0.6313498337005227, 0.05468749999999993),
    glm::vec3(-0.9976073333593232, -0.05704147194322919, 0.03906249999999996),
    glm::vec3(0.6974144443248256, 0.7162847034809865, 0.023437499999999938),
    glm::vec3(-0.03041576832268194, -0.9995068013180756, 0.007812500000000042)
};
    // Transform pre-calculated samples to align with given normal
    void GetHemisphereSamples(const glm::vec3& normal, std::vector<glm::vec3>& result )
    {
        result.reserve(RAY_PERFACE);

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
    
    void Scene::GenerateAmbientCubeCPU()
    {
        if (GTriangles.size() < 3)
        {
            return;
        }

        const auto timer = std::chrono::high_resolution_clock::now();
        std::atomic<int> rayCount{0};
        constexpr int cubeSize = CUBE_SIZE;
        std::vector<AmbientCube> ambientCubes(cubeSize * cubeSize * cubeSize);
        
        // Create a lambda for processing a single z-slice
        auto processSlice = [this, &ambientCubes, &rayCount](int z)
        {
            constexpr int raysPerFace = 4;
            const glm::vec3 directions[6] = {
                glm::vec3(0, 0, 1),  // PosZ
                glm::vec3(0, 0, -1), // NegZ
                glm::vec3(0, 1, 0),  // PosY
                glm::vec3(0, -1, 0), // NegY
                glm::vec3(1, 0, 0),  // PosX
                glm::vec3(-1, 0, 0)  // NegX
            };
        
            const float CUBE_UNIT = 0.2f;
            const glm::vec3 CUBE_OFFSET = glm::vec3(-50, -49.9, -50) * CUBE_UNIT;
        
            // Process single z-slice
            for (int y = 0; y < cubeSize; y++)
            {
                for (int x = 0; x < cubeSize; x++)
                {
                    tinybvh::Ray ray;
                    glm::vec3 probePos = glm::vec3(x, y, z) * CUBE_UNIT + CUBE_OFFSET;
                    fastCopyVec3(ray.O, probePos);
                    
                    AmbientCube& cube = ambientCubes[z * cubeSize * cubeSize + y * cubeSize + x];
                    cube.Info = glm::vec4(1, 0, 0, 0);
        
                    for (int face = 0; face < 6; face++)
                    {
                        glm::vec3 baseDir = directions[face];
                        glm::vec3 u = glm::normalize(glm::cross(
                            baseDir, glm::abs(baseDir.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                        glm::vec3 v = glm::normalize(glm::cross(baseDir, u));
        
                        float occlusion = 0.0f;

                        std::vector<glm::vec3> hemisphereSamples;
                        GetHemisphereSamples(baseDir, hemisphereSamples);
                        
                        for (int i = 0; i < RAY_PERFACE; i++)
                        {
                            fastCopyVec3(ray.rD, hemisphereSamples[i]);
                            fastCopyVec3(ray.D, ray.rD);
                            ray.hit.t = 15.0f;
                            
                            GCpuBvh.Intersect(ray);
                            rayCount.fetch_add(1, std::memory_order_relaxed);
    
                            if (ray.hit.t < 10.0f)
                            {
                                const glm::vec3& hitNormal = GExtInfos[ray.hit.prim].normal;
                                float dotProduct = glm::dot(hemisphereSamples[i], hitNormal);
                                
                                if (dotProduct < 0.0f)
                                {
                                    occlusion += 1.0f;
                                }
                                else
                                {
                                    cube.Info = glm::vec4(0, 0, 0, 0);
                                    break;
                                }
                            }
                        }
        
                        occlusion /= RAY_PERFACE;
                        float visibility = 1.0f - occlusion;
        
                        switch (face)
                        {
                        case 0: cube.PosZ = glm::vec4(visibility); break;
                        case 1: cube.NegZ = glm::vec4(visibility); break;
                        case 2: cube.PosY = glm::vec4(visibility); break;
                        case 3: cube.NegY = glm::vec4(visibility); break;
                        case 4: cube.PosX = glm::vec4(visibility); break;
                        case 5: cube.NegX = glm::vec4(visibility); break;
                        }
                    }
                }
            }
        };
        
        // Distribute tasks for each z-slice
        for (int z = 0; z < cubeSize; z++)
        {
            TaskCoordinator::GetInstance()->AddParralledTask([processSlice, z](ResTask& task)
            {
                processSlice(z);
            });
        }

        TaskCoordinator::GetInstance()->WaitForAllParralledTask();

        // Upload to GPU
        AmbientCube* data = reinterpret_cast<AmbientCube*>(ambientCubeBufferMemory_->Map(0, sizeof(AmbientCube) * ambientCubes.size()));
        std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(AmbientCube));
        ambientCubeBufferMemory_->Unmap();

        double elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();

        fmt::print("gen gi data takes: {:.0f}s, {}W rays\n", elapsed, rayCount / 10000);
    }

    void Scene::Tick(float DeltaSeconds)
    {
        float DurationMax = 0;

        for (auto& track : tracks_)
        {
            if (!track.Playing()) continue;
            DurationMax = glm::max(DurationMax, track.Duration_);
        }

        for (auto& track : tracks_)
        {
            if (!track.Playing()) continue;
            track.Time_ += DeltaSeconds;
            if (track.Time_ > DurationMax)
            {
                track.Time_ = 0;
            }
            Node* node = GetNode(track.NodeName_);
            if (node)
            {
                glm::vec3 translation = node->Translation();
                glm::quat rotation = node->Rotation();
                glm::vec3 scaling = node->Scale();

                track.Sample(track.Time_, translation, rotation, scaling);

                node->SetTranslation(translation);
                node->SetRotation(rotation);
                node->SetScale(scaling);
                node->RecalcTransform(true);

                MarkDirty();

                // temporal if camera node, request override
                if (node->GetName() == "Shot.BlueCar")
                {
                    requestOverrideModelView = true;
                    overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0, 0, -1), glm::vec3(0.0f, 1.0f, 0.0f));
                }
            }
        }
    }

    void Scene::UpdateMaterial()
    {
        if (materials_.empty()) return;

        gpuMaterials_.clear();
        for (auto& material : materials_)
        {
            gpuMaterials_.push_back(material.gpuMaterial_);
        }

        Material* data = reinterpret_cast<Material*>(materialBufferMemory_->Map(0, sizeof(Material) * gpuMaterials_.size()));
        std::memcpy(data, gpuMaterials_.data(), gpuMaterials_.size() * sizeof(Material));
        materialBufferMemory_->Unmap();
    }

    bool Scene::UpdateNodes()
    {
        // this can move to thread task
        if (nodes_.size() > 0)
        {
            if (sceneDirty_)
            {
                sceneDirty_ = false;
                {
                    PERFORMANCEAPI_INSTRUMENT_COLOR("Scene::PrepareSceneNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
                    nodeProxys.clear();
                    indirectDrawBufferInstanced.clear();

                    uint32_t indexOffset = 0;
                    uint32_t vertexOffset = 0;
                    uint32_t nodeOffsetBatched = 0;

                    static std::unordered_map<uint32_t, std::vector<NodeProxy>> nodeProxysMapByModel;
                    for (auto& [key, value] : nodeProxysMapByModel)
                    {
                        value.clear();
                    }

                    for (auto& node : nodes_)
                    {
                        if (node->IsVisible())
                        {
                            auto modelId = node->GetModel();
                            glm::mat4 combined;
                            if (node->TickVelocity(combined))
                            {
                                MarkDirty();
                            }

                            NodeProxy proxy = node->GetNodeProxy();
                            proxy.combinedPrevTS = combined;
                            nodeProxysMapByModel[modelId].push_back(proxy);
                        }
                    }

                    int modelCount = static_cast<int>(models_.size());
                    for (int i = 0; i < modelCount; i++)
                    {
                        if (nodeProxysMapByModel.find(i) != nodeProxysMapByModel.end())
                        {
                            auto& nodesOfThisModel = nodeProxysMapByModel[i];

                            // draw indirect buffer, instanced, this could be generate in gpu
                            VkDrawIndexedIndirectCommand cmd{};
                            cmd.firstIndex = indexOffset;
                            cmd.indexCount = models_[i].NumberOfIndices();
                            cmd.vertexOffset = static_cast<int32_t>(vertexOffset);
                            cmd.firstInstance = nodeOffsetBatched;
                            cmd.instanceCount = static_cast<uint32_t>(nodesOfThisModel.size());

                            indirectDrawBufferInstanced.push_back(cmd);
                            nodeOffsetBatched += static_cast<uint32_t>(nodesOfThisModel.size());

                            // fill the nodeProxy
                            nodeProxys.insert(nodeProxys.end(), nodesOfThisModel.begin(), nodesOfThisModel.end());
                        }

                        indexOffset += models_[i].NumberOfIndices();
                        vertexOffset += models_[i].NumberOfVertices();
                    }

                    NodeProxy* data = reinterpret_cast<NodeProxy*>(nodeMatrixBufferMemory_->Map(0, sizeof(NodeProxy) * nodeProxys.size()));
                    std::memcpy(data, nodeProxys.data(), nodeProxys.size() * sizeof(NodeProxy));
                    nodeMatrixBufferMemory_->Unmap();

                    VkDrawIndexedIndirectCommand* diic = reinterpret_cast<VkDrawIndexedIndirectCommand*>(indirectDrawBufferMemory_->Map(
                        0, sizeof(VkDrawIndexedIndirectCommand) * indirectDrawBufferInstanced.size()));
                    std::memcpy(diic, indirectDrawBufferInstanced.data(), indirectDrawBufferInstanced.size() * sizeof(VkDrawIndexedIndirectCommand));
                    indirectDrawBufferMemory_->Unmap();

                    indirectDrawBatchCount_ = static_cast<uint32_t>(indirectDrawBufferInstanced.size());
                }
                return true;
            }
        }
        return false;
    }

    Node* Scene::GetNode(std::string name)
    {
        for (auto& node : nodes_)
        {
            if (node->GetName() == name)
            {
                return node.get();
            }
        }
        return nullptr;
    }

    Node* Scene::GetNodeByInstanceId(uint32_t id)
    {
        for (auto& node : nodes_)
        {
            if (node->GetInstanceId() == id)
            {
                return node.get();
            }
        }
        return nullptr;
    }

    const Model* Scene::GetModel(uint32_t id) const
    {
        if (id < models_.size())
        {
            return &models_[id];
        }
        return nullptr;
    }

    const FMaterial* Scene::GetMaterial(uint32_t id) const
    {
        if (id < materials_.size())
        {
            return &materials_[id];
        }
        return nullptr;
    }

    void Scene::OverrideModelView(glm::mat4& OutMatrix)
    {
        if (requestOverrideModelView)
        {
            requestOverrideModelView = false;
            OutMatrix = overrideModelView;
        }
    }
}
