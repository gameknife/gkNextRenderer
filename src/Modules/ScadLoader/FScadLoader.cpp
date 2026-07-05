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
#include <cstdint>

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

        bool AttachSceneMeshesToNode(
            const std::string& baseName,
            const std::vector<scad::SceneMeshBucket>& meshBuckets,
            size_t startBucket,
            size_t maxBucketCount,
            double scale,
            float smoothAngleDegrees,
            std::shared_ptr<Node> node,
            std::vector<Model>& models,
            std::vector<FMaterial>& materials)
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

                const uint32_t materialIdx = static_cast<uint32_t>(materials.size());
                materials.push_back({ScadMaterialFromColor(bucket.color),
                                     fmt::format("{}__material_{}", baseName, bucketIndex)});
                nodeMaterials[sectionIndex] = materialIdx;
                ++sectionIndex;
            }

            if (vertices.empty() || sectionIndex == 0)
            {
                return false;
            }

            Model model = FProcModel::CreateFromBuffers(
                fmt::format("{}__mesh_{}_{}", baseName, startBucket, endBucket - 1),
                std::move(vertices),
                std::move(indices),
                false);
            model.SetSectionCount(sectionIndex);

            const uint32_t modelIdx = static_cast<uint32_t>(models.size());
            models.push_back(std::move(model));

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
            std::vector<FMaterial>& materials)
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
                        materials);
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
                            materials);
                    }
                }
            }

            for (const scad::SceneNode& child : sceneNode.children)
            {
                BuildScadSceneNodeRecursive(child, node, scale, smoothAngleDegrees, nodes, models, materials);
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
        size_t rootCount = 0;
        for (const scad::SceneNode& root : result.roots)
        {
            BuildScadSceneNodeRecursive(root, nullptr, scale, options.smoothAngleDegrees, nodes, models, materials);
            ++rootCount;
        }

        // ---- Camera + environment ----
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SunRotation = 0.35f;
        cameraInit.SunIntensity = 500.0f;
        cameraInit.SkyIntensity = 80.0f;

        Camera defaultCam = FSceneLoader::AutoFocusCamera(cameraInit, nodes, models);
        if (cameraInit.cameras.empty())
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        SPDLOG_INFO("SCAD: loaded {} -> {} root nodes, {} total nodes, {} triangles ({} warnings)",
                    filename, rootCount, nodes.size(), result.triangleCount, result.warningCount);
        return true;
    }
} // namespace Assets
