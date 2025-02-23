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
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "Nodes", flags, sizeof(NodeProxy) * 65535, nodeMatrixBuffer_, nodeMatrixBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_,
                                                    indirectDrawBufferMemory_); // support 65535 nodes


        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "Materials", flags, sizeof(Material) * 4096, materialBuffer_, materialBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "AmbientCubes", flags, Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z * sizeof(Assets::AmbientCube), ambientCubeBuffer_,
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
            //GCpuBvh.Convert(tinybvh::BVH::WALD_32BYTE, tinybvh::BVH::ALT_SOA, true);
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

#define RAY_PERFACE 32

    inline void fastCopyVec3(tinybvh::bvhvec3& target, glm::vec3& source)
    {
        memcpy(&target, &source, sizeof(glm::vec3));
    }

    inline void fastCopyVec3(tinybvh::bvhvec3& target, tinybvh::bvhvec3& source)
    {
        memcpy(&target, &source, sizeof(tinybvh::bvhvec3));
    }

    // Pre-calculated hemisphere samples in the positive Z direction (up)
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
        for (const auto& sample : hemisphereVectors32)
        {
            result.push_back(transform * sample);
        }
    }

    void BlurAmbientCubes(std::vector<AmbientCube>& ambientCubes, const std::vector<AmbientCube>& ambientCubesCopy)
    {
        const float centerWeight = 1.0f;
        const float neighborWeight = 1.0f;
        const float weightSum = centerWeight + (6.0f * neighborWeight);

        // Create a lambda for processing a single z-slice
        auto processSlice = [&ambientCubes, &ambientCubesCopy, centerWeight, neighborWeight, weightSum](int z)
        {
            // Process only interior cubes (skip border)
            if (z <= 0 || z >= Assets::CUBE_SIZE_Z - 1) return;

            for (int y = 1; y < Assets::CUBE_SIZE_XY - 1; y++)
            {
                for (int x = 1; x < Assets::CUBE_SIZE_XY - 1; x++)
                {
                    int idx = z * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + y * Assets::CUBE_SIZE_XY + x;

                    // Get indices of 6 neighboring cubes
                    int idxPosX = z * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + y * Assets::CUBE_SIZE_XY + (x + 1);
                    int idxNegX = z * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + y * Assets::CUBE_SIZE_XY + (x - 1);
                    int idxPosY = z * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + (y + 1) * Assets::CUBE_SIZE_XY + x;
                    int idxNegY = z * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + (y - 1) * Assets::CUBE_SIZE_XY + x;
                    int idxPosZ = (z + 1) * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + y * Assets::CUBE_SIZE_XY + x;
                    int idxNegZ = (z - 1) * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + y * Assets::CUBE_SIZE_XY + x;

                    // Skip if center cube is not active
                    if (ambientCubesCopy[idx].Info.x != 1) continue;

                    uint32_t* TargetAddress = (uint32_t*)&ambientCubes[idx];
                    uint32_t* SourceAddressCenter = (uint32_t*)&ambientCubesCopy[idx];
                    uint32_t* SourceAddressPosX = (uint32_t*)&ambientCubesCopy[idxPosX];
                    uint32_t* SourceAddressNegX = (uint32_t*)&ambientCubesCopy[idxNegX];
                    uint32_t* SourceAddressPosY = (uint32_t*)&ambientCubesCopy[idxPosY];
                    uint32_t* SourceAddressNegY = (uint32_t*)&ambientCubesCopy[idxNegY];
                    uint32_t* SourceAddressPosZ = (uint32_t*)&ambientCubesCopy[idxPosZ];
                    uint32_t* SourceAddressNegZ = (uint32_t*)&ambientCubesCopy[idxNegZ];

                    for (int face = 0; face < 6; face++)
                    {
                        TargetAddress[face] = glm::packUnorm4x8((
                            glm::unpackUnorm4x8(SourceAddressCenter[face]) * centerWeight +
                            glm::unpackUnorm4x8(SourceAddressPosX[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressNegX[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressPosY[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressNegY[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressPosZ[face]) * neighborWeight +
                            glm::unpackUnorm4x8(SourceAddressNegZ[face]) * neighborWeight
                        ) / weightSum);
                    }
                }
            }
        };

        // Distribute tasks for each z-slice
        for (int z = 0; z < Assets::CUBE_SIZE_Z; z++)
        {
            TaskCoordinator::GetInstance()->AddParralledTask([processSlice, z](ResTask& task)
            {
                processSlice(z);
            });
        }

        TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    }

    bool IsInShadow(const glm::vec3& point, const glm::vec3& lightPos, const tinybvh::BVH& bvh)
    {
        glm::vec3 direction = glm::normalize(lightPos - point);
        tinybvh::Ray shadowRay(tinybvh::bvhvec3(point.x, point.y, point.z), tinybvh::bvhvec3(direction.x, direction.y, direction.z));

        bvh.Intersect(shadowRay);

        // If the ray hits something before reaching the light, the point is in shadow
        float distanceToLight = glm::length(lightPos - point);
        return shadowRay.hit.t < distanceToLight;
    }

    void Scene::GenerateAmbientCubeCPU()
    {
        if (GTriangles.size() < 3)
        {
            return;
        }

        const auto timer = std::chrono::high_resolution_clock::now();

        std::vector<AmbientCube> ambientCubes(Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z);
        std::vector<AmbientCube> ambientCubesCopy(Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z);
        ambientCubeProxys_.resize(Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z);

        // Create a lambda for processing a single z-slice
        auto processSlice = [this, &ambientCubes, &ambientCubesCopy](int x, int y, int z)
        {
            const glm::vec3 directions[6] = {
                glm::vec3(0, 0, 1), // PosZ
                glm::vec3(0, 0, -1), // NegZ
                glm::vec3(0, 1, 0), // PosY
                glm::vec3(0, -1, 0), // NegY
                glm::vec3(1, 0, 0), // PosX
                glm::vec3(-1, 0, 0) // NegX
            };

            glm::vec3 probePos = glm::vec3(x, y, z) * CUBE_UNIT + CUBE_OFFSET;

            AmbientCube& cube = ambientCubes[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x];
            cube.Info = glm::vec4(1, 1, 0, 0);

            for (int face = 0; face < 6; face++)
            {
                glm::vec3 baseDir = directions[face];

                float occlusion = 0.0f;

                std::vector<glm::vec3> hemisphereSamples;
                GetHemisphereSamples(baseDir, hemisphereSamples);

                bool nextCube = false;
                for (int i = 0; i < RAY_PERFACE; i++)
                {
                    tinybvh::Ray ray(tinybvh::bvhvec3(probePos.x, probePos.y, probePos.z),
                                     tinybvh::bvhvec3(hemisphereSamples[i].x, hemisphereSamples[i].y, hemisphereSamples[i].z));

                    GCpuBvh.Intersect(ray);
                    //rayCount.fetch_add(1, std::memory_order_relaxed);

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
                            cube.Info.x = 0;
                            cube.Info.y = 0;
                            nextCube = true;
                            break;
                        }
                    }
                }

                if (nextCube)
                {
                    ambientCubeProxys_[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x] = {probePos, (cube.Info.x == 1)};
                    continue;
                }

                occlusion /= RAY_PERFACE;
                float visibility = 1.0f - occlusion;
                vec4 indirectColor = vec4(visibility);
                uint packedColor = glm::packUnorm4x8(indirectColor);

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

            glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.5f));

            IsInShadow(probePos, probePos + lightDir * -10000.0f, GCpuBvh) ? cube.Info.y = 0 : cube.Info.y = 1;

            ambientCubeProxys_[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x] = {probePos, (cube.Info.x == 1)};
            ambientCubesCopy[y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x] = cube;
        };

        // Distribute tasks for each z-slice
        for (int x = 0; x < Assets::CUBE_SIZE_XY; x++)
        {
            TaskCoordinator::GetInstance()->AddParralledTask([processSlice, x](ResTask& task)
            {
                for (int z = 0; z < Assets::CUBE_SIZE_XY; z++)
                    for (int y = 0; y < Assets::CUBE_SIZE_Z; y++)
                        processSlice(x, y, z);
            });
        }

        TaskCoordinator::GetInstance()->WaitForAllParralledTask();

        BlurAmbientCubes(ambientCubes, ambientCubesCopy);

        // Upload to GPU
        AmbientCube* data = reinterpret_cast<AmbientCube*>(ambientCubeBufferMemory_->Map(0, sizeof(AmbientCube) * ambientCubes.size()));
        std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(AmbientCube));
        ambientCubeBufferMemory_->Unmap();

        double elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
            std::chrono::high_resolution_clock::now() - timer).count();

        fmt::print("gen gi data takes: {:.0f}s\n", elapsed);
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
