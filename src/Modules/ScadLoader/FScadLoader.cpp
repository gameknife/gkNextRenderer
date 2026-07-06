#include "Modules/ScadLoader/FScadLoader.h"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Engine/Assets/Loaders/FSceneLoader.h"
#include "Engine/Runtime/Components/RenderComponent.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace Assets
{
    namespace
    {
        Material ScadMaterialFromColor(const glm::vec4& color)
        {
            const glm::vec3 rgb(color.r, color.g, color.b);
            const float alpha = color.a;
            if (alpha < 0.99f)
            {
                Material material = Material::Dielectric(1.45f, 0.0f);
                material.Diffuse = glm::vec4(rgb, alpha);
                return material;
            }
            return Material::Lambertian(rgb);
        }

        uint32_t QuantizeMaterialColor(const glm::vec4& color)
        {
            auto q = [](float v) -> uint32_t
            {
                return static_cast<uint32_t>(std::clamp(std::lround(v * 255.0f), 0l, 255l));
            };
            return q(color.r) | (q(color.g) << 8u) | (q(color.b) << 16u) | (q(color.a) << 24u);
        }

        std::string SanitizeScadMaterialName(const std::string& sourceName)
        {
            std::string clean;
            clean.reserve(sourceName.size());
            for (char ch : sourceName)
            {
                const auto uch = static_cast<unsigned char>(ch);
                if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.')
                {
                    clean.push_back(ch);
                }
                else if (std::isspace(uch))
                {
                    clean.push_back('_');
                }
            }
            return clean;
        }

        std::string ScadMaterialName(const glm::vec4& color, const std::string& sourceName)
        {
            std::string clean = SanitizeScadMaterialName(sourceName);
            if (!clean.empty())
            {
                return clean;
            }

            return fmt::format("scad_rgba_{:08x}", QuantizeMaterialColor(color));
        }

        void AppendBytes(std::string& key, const void* data, size_t size)
        {
            key.append(static_cast<const char*>(data), size);
        }

        template <typename T>
        void AppendValue(std::string& key, const T& value)
        {
            AppendBytes(key, &value, sizeof(T));
        }

        void AppendVertexKey(std::string& key, const Vertex& vertex)
        {
            AppendValue(key, vertex.Position);
            AppendValue(key, vertex.Normal);
            AppendValue(key, vertex.Tangent);
            AppendValue(key, vertex.TexCoord);
            AppendValue(key, vertex.MaterialIndex);
        }

        struct ScadBuildCache
        {
            std::unordered_map<uint32_t, uint32_t> materialByColor;
            std::unordered_map<std::string, uint32_t> modelByMesh;
        };

        uint32_t GetOrCreateScadMaterial(
            const glm::vec4& color,
            const std::string& sourceName,
            std::vector<FMaterial>& materials,
            ScadBuildCache& cache)
        {
            const uint32_t colorKey = QuantizeMaterialColor(color);
            auto found = cache.materialByColor.find(colorKey);
            if (found != cache.materialByColor.end())
            {
                return found->second;
            }

            const uint32_t materialIdx = static_cast<uint32_t>(materials.size());
            materials.push_back({ScadMaterialFromColor(color), ScadMaterialName(color, sourceName)});
            cache.materialByColor[colorKey] = materialIdx;
            return materialIdx;
        }

        bool AttachSceneMeshesToNode(
            const std::string& baseName,
            const std::vector<scad::SceneMeshBucket>& meshBuckets,
            size_t startBucket,
            size_t maxBucketCount,
            double scale,
            float smoothAngleDegrees,
            std::shared_ptr<Node> node,
            std::vector<Model>& models,
            std::vector<FMaterial>& materials,
            ScadBuildCache& cache)
        {
            const size_t endBucket = std::min(meshBuckets.size(), startBucket + maxBucketCount);
            if (startBucket >= endBucket)
            {
                return false;
            }

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::array<uint32_t, 16> nodeMaterials = {0};
            uint32_t sectionIndex = 0;

            for (size_t bucketIndex = startBucket; bucketIndex < endBucket; ++bucketIndex)
            {
                const scad::SceneMeshBucket& bucket = meshBuckets[bucketIndex];
                const size_t triCount = bucket.tris.size() / 3;
                if (triCount == 0)
                {
                    continue;
                }

                std::vector<glm::vec3> localPos(triCount * 3);
                for (size_t i = 0; i < localPos.size(); ++i)
                {
                    localPos[i] = scad::ScadToWorldPos(bucket.tris[i], scale);
                }
                const std::vector<glm::vec3> normals = scad::ScadComputeSmoothNormals(localPos, smoothAngleDegrees);

                const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
                vertices.reserve(vertices.size() + localPos.size());
                indices.reserve(indices.size() + localPos.size());
                for (uint32_t i = 0; i < static_cast<uint32_t>(localPos.size()); ++i)
                {
                    vertices.push_back(Vertex{localPos[i], normals[i], glm::vec4(1, 0, 0, 1), glm::vec2(0, 0), sectionIndex});
                    indices.push_back(vertexOffset + i);
                }

                nodeMaterials[sectionIndex] = GetOrCreateScadMaterial(bucket.color, bucket.materialName, materials, cache);
                ++sectionIndex;
            }

            if (vertices.empty() || sectionIndex == 0)
            {
                return false;
            }

            std::string meshKey;
            meshKey.reserve(vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32_t) + 32);
            AppendValue(meshKey, sectionIndex);
            const uint64_t vertexCount = static_cast<uint64_t>(vertices.size());
            const uint64_t indexCount = static_cast<uint64_t>(indices.size());
            AppendValue(meshKey, vertexCount);
            AppendValue(meshKey, indexCount);
            for (const Vertex& vertex : vertices)
            {
                AppendVertexKey(meshKey, vertex);
            }
            if (!indices.empty())
            {
                AppendBytes(meshKey, indices.data(), indices.size() * sizeof(uint32_t));
            }

            uint32_t modelIdx = 0;
            auto cachedModel = cache.modelByMesh.find(meshKey);
            if (cachedModel != cache.modelByMesh.end())
            {
                modelIdx = cachedModel->second;
            }
            else
            {
                Model model = FProcModel::CreateFromBuffers(
                    fmt::format("{}__mesh_{}_{}", baseName, startBucket, endBucket - 1),
                    std::move(vertices),
                    std::move(indices),
                    false);
                model.SetSectionCount(sectionIndex);

                modelIdx = static_cast<uint32_t>(models.size());
                models.push_back(std::move(model));
                cache.modelByMesh.emplace(std::move(meshKey), modelIdx);
            }

            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetRayCastVisible(true);
            renderComp->SetMaterials(nodeMaterials);
            node->AddComponent(renderComp);
            return true;
        }

        void BuildScadSceneNodeRecursive(
            const scad::SceneNode& sceneNode,
            const std::shared_ptr<Node>& parent,
            double scale,
            float smoothAngleDegrees,
            std::vector<std::shared_ptr<Node>>& nodes,
            std::vector<Model>& models,
            std::vector<FMaterial>& materials,
            ScadBuildCache& cache)
        {
            glm::vec3 localTranslation(0.0f);
            glm::quat localRotation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 localScale(1.0f);
            scad::ScadLocalToEngineTRS(sceneNode.localTransform, scale, localTranslation, localRotation, localScale);

            auto node = Node::CreateNode(
                sceneNode.name,
                localTranslation,
                localRotation,
                localScale,
                static_cast<uint32_t>(sceneNode.instanceId));
            if (parent)
            {
                node->SetParent(parent);
            }
            nodes.push_back(node);

            if (!sceneNode.meshes.empty())
            {
                constexpr size_t kMaxMaterialsPerNode = 16;
                if (sceneNode.meshes.size() <= kMaxMaterialsPerNode)
                {
                    AttachSceneMeshesToNode(
                        sceneNode.name,
                        sceneNode.meshes,
                        0,
                        kMaxMaterialsPerNode,
                        scale,
                        smoothAngleDegrees,
                        node,
                        models,
                        materials,
                        cache);
                }
                else
                {
                    for (size_t start = 0; start < sceneNode.meshes.size(); start += kMaxMaterialsPerNode)
                    {
                        auto chunkNode = Node::CreateNode(
                            fmt::format("{}__render", sceneNode.name),
                            glm::vec3(0.0f),
                            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                            glm::vec3(1.0f),
                            static_cast<uint32_t>(nodes.size()));
                        chunkNode->SetParent(node);
                        nodes.push_back(chunkNode);
                        AttachSceneMeshesToNode(
                            sceneNode.name,
                            sceneNode.meshes,
                            start,
                            kMaxMaterialsPerNode,
                            scale,
                            smoothAngleDegrees,
                            chunkNode,
                            models,
                            materials,
                            cache);
                    }
                }
            }

            for (const scad::SceneNode& child : sceneNode.children)
            {
                BuildScadSceneNodeRecursive(child, node, scale, smoothAngleDegrees, nodes, models, materials, cache);
            }
        }
    } // namespace

    bool FScadLoader::LoadScadScene(
        const std::string& filename,
        EnvironmentSetting& cameraInit,
        std::vector<std::shared_ptr<Node>>& nodes,
        std::vector<Model>& models,
        std::vector<FMaterial>& materials,
        std::vector<LightObject>& /*lights*/,
        std::vector<AnimationTrack>& /*tracks*/,
        std::vector<Skeleton>& /*skeletons*/,
        const ScadLoadOptions& options)
    {
        // ---- Resolve the use/include closure ----
        scad::ScadProgram program;
        std::string programErr;
        if (!scad::LoadScadProgram(filename, program, programErr))
        {
            SPDLOG_ERROR("SCAD: {}", programErr);
            return false;
        }

        // ---- Evaluate ----
        scad::SceneEvalResult result;
        std::string evalErr;
        scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions, options, result, evalErr);

        if (result.roots.empty())
        {
            SPDLOG_WARN("SCAD: no geometry produced from {}", filename);
        }

        const double scale = static_cast<double>(options.scadToWorldScale > 0.0f ? options.scadToWorldScale : 1.0f);

        // ---- Recreate the SCAD user-module hierarchy ----
        ScadBuildCache buildCache;
        size_t rootCount = 0;
        for (const scad::SceneNode& root : result.roots)
        {
            BuildScadSceneNodeRecursive(root, nullptr, scale, options.smoothAngleDegrees, nodes, models, materials, buildCache);
            ++rootCount;
        }

        // ---- Camera + environment ----
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SunRotation = 0.35f;
        cameraInit.SunIntensity = 500.0f;
        cameraInit.SkyIntensity = 80.0f;

        Camera defaultCam = FSceneLoader::AutoFocusCamera(cameraInit, nodes, models, true);
        if (cameraInit.cameras.empty())
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        SPDLOG_INFO("SCAD: loaded {} -> {} root nodes, {} total nodes, {} triangles ({} warnings)",
                    filename, rootCount, nodes.size(), result.triangleCount, result.warningCount);
        return true;
    }
} // namespace Assets
