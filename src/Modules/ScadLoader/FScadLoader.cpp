#include "Modules/ScadLoader/FScadLoader.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Modules/ScadLoader/FScadTerrain.h"
#include "Engine/Assets/Loaders/LoaderUtils.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <xxhash.h>

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

        struct ScadMeshFingerprint
        {
            uint64_t vertexHash = 0;
            uint64_t indexHash = 0;
            uint64_t vertexCount = 0;
            uint64_t indexCount = 0;
            uint32_t sectionCount = 0;

            bool operator==(const ScadMeshFingerprint&) const = default;
        };

        struct ScadMeshFingerprintHash
        {
            size_t operator()(const ScadMeshFingerprint& key) const
            {
                uint64_t hash = key.vertexHash;
                hash ^= key.indexHash + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
                hash ^= key.vertexCount + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
                hash ^= key.indexCount + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
                hash ^= key.sectionCount + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
                return static_cast<size_t>(hash);
            }
        };

        ScadMeshFingerprint FingerprintScadMesh(
            const std::vector<Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            uint32_t sectionCount)
        {
            ScadMeshFingerprint fingerprint;
            fingerprint.vertexHash =
                vertices.empty() ? 0 : XXH64(vertices.data(), vertices.size() * sizeof(Vertex), 0);
            fingerprint.indexHash =
                indices.empty() ? 0 : XXH64(indices.data(), indices.size() * sizeof(uint32_t), 0);
            fingerprint.vertexCount = vertices.size();
            fingerprint.indexCount = indices.size();
            fingerprint.sectionCount = sectionCount;
            return fingerprint;
        }

        bool ScadMeshEquals(
            const Model& model,
            const std::vector<Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            uint32_t sectionCount)
        {
            return model.SectionCount() == sectionCount &&
                   model.CPUVertices().size() == vertices.size() &&
                   model.CPUIndices().size() == indices.size() &&
                   std::equal(model.CPUVertices().begin(), model.CPUVertices().end(), vertices.begin()) &&
                   model.CPUIndices() == indices;
        }

        struct ScadBuildCache
        {
            std::unordered_map<uint32_t, uint32_t> materialByColor;
            std::unordered_map<ScadMeshFingerprint, std::vector<uint32_t>, ScadMeshFingerprintHash> modelByMesh;
            // Scene-eval instanceId -> engine node + accumulated engine-space
            // world transform (for the TerrainComponent hookup).
            std::unordered_map<uint64_t, std::pair<std::shared_ptr<Node>, glm::dmat4>> nodeByInstanceId;
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
            const std::vector<Scad::SceneMeshBucket>& meshBuckets,
            size_t startBucket,
            size_t maxBucketCount,
            double scale,
            float smoothAngleDegrees,
            bool rayCastVisible,
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
                const Scad::SceneMeshBucket& bucket = meshBuckets[bucketIndex];
                const size_t triCount = bucket.tris.size() / 3;
                if (triCount == 0)
                {
                    continue;
                }

                std::vector<glm::vec3> localPos(triCount * 3);
                for (size_t i = 0; i < localPos.size(); ++i)
                {
                    localPos[i] = Scad::ScadToWorldPos(bucket.tris[i], scale);
                }
                // Terrain buckets stay faceted: smoothing would wash the low-poly
                // facets on gentle slopes into a soft gradient.
                const float bucketSmoothAngle = bucket.faceted ? 0.0f : smoothAngleDegrees;
                const std::vector<glm::vec3> normals = Scad::ScadComputeSmoothNormals(localPos, bucketSmoothAngle);

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

            const ScadMeshFingerprint fingerprint = FingerprintScadMesh(vertices, indices, sectionIndex);
            uint32_t modelIdx = 0;
            bool foundCachedModel = false;
            auto cachedModels = cache.modelByMesh.find(fingerprint);
            if (cachedModels != cache.modelByMesh.end())
            {
                for (uint32_t candidate : cachedModels->second)
                {
                    if (candidate < models.size() &&
                        ScadMeshEquals(models[candidate], vertices, indices, sectionIndex))
                    {
                        modelIdx = candidate;
                        foundCachedModel = true;
                        break;
                    }
                }
            }
            if (!foundCachedModel)
            {
                Model model = FProcModel::CreateFromBuffers(
                    fmt::format("{}__mesh_{}_{}", baseName, startBucket, endBucket - 1),
                    std::move(vertices),
                    std::move(indices),
                    false);
                model.SetSectionCount(sectionIndex);

                modelIdx = static_cast<uint32_t>(models.size());
                models.push_back(std::move(model));
                cache.modelByMesh[fingerprint].push_back(modelIdx);
            }

            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetRayCastVisible(rayCastVisible);
            renderComp->SetMaterials(nodeMaterials);
            node->AddComponent(renderComp);
            return true;
        }

        void BuildScadSceneNodeRecursive(
            const Scad::SceneNode& sceneNode,
            const std::shared_ptr<Node>& parent,
            const glm::dmat4& parentWorld,
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
            Scad::ScadLocalToEngineTRS(sceneNode.localTransform, scale, localTranslation, localRotation, localScale);

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

            const glm::dmat4 nodeWorld = parentWorld *
                (glm::translate(glm::dmat4(1.0), glm::dvec3(localTranslation)) *
                 glm::toMat4(glm::dquat(localRotation)) *
                 glm::scale(glm::dmat4(1.0), glm::dvec3(localScale)));
            cache.nodeByInstanceId[sceneNode.instanceId] = {node, nodeWorld};

            if (!sceneNode.meshes.empty())
            {
                // Terrain water surfaces get their own child node: translucent,
                // and invisible to raycasts so NavGrid/picking hit the river bed
                // (or a bridge deck) instead of the water plane.
                std::vector<Scad::SceneMeshBucket> solidBuckets;
                std::vector<Scad::SceneMeshBucket> waterBuckets;
                solidBuckets.reserve(sceneNode.meshes.size());
                for (const Scad::SceneMeshBucket& bucket : sceneNode.meshes)
                {
                    (bucket.terrainWater ? waterBuckets : solidBuckets).push_back(bucket);
                }

                constexpr size_t kMaxMaterialsPerNode = 16;
                if (solidBuckets.size() <= kMaxMaterialsPerNode)
                {
                    AttachSceneMeshesToNode(
                        sceneNode.name,
                        solidBuckets,
                        0,
                        kMaxMaterialsPerNode,
                        scale,
                        smoothAngleDegrees,
                        true,
                        node,
                        models,
                        materials,
                        cache);
                }
                else
                {
                    for (size_t start = 0; start < solidBuckets.size(); start += kMaxMaterialsPerNode)
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
                            solidBuckets,
                            start,
                            kMaxMaterialsPerNode,
                            scale,
                            smoothAngleDegrees,
                            true,
                            chunkNode,
                            models,
                            materials,
                            cache);
                    }
                }

                if (!waterBuckets.empty())
                {
                    auto waterNode = Node::CreateNode(
                        fmt::format("{}__water", sceneNode.name),
                        glm::vec3(0.0f),
                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        glm::vec3(1.0f),
                        static_cast<uint32_t>(nodes.size()));
                    waterNode->SetParent(node);
                    nodes.push_back(waterNode);
                    AttachSceneMeshesToNode(
                        fmt::format("{}__water", sceneNode.name),
                        waterBuckets,
                        0,
                        kMaxMaterialsPerNode,
                        scale,
                        smoothAngleDegrees,
                        false,
                        waterNode,
                        models,
                        materials,
                        cache);
                }
            }

            for (const Scad::SceneNode& child : sceneNode.children)
            {
                BuildScadSceneNodeRecursive(child, node, nodeWorld, scale, smoothAngleDegrees, nodes, models, materials, cache);
            }
        }

        // Attaches a TerrainComponent for each gk_terrain payload: the exact
        // triangulated grid the render mesh was built from, plus the combined
        // terrain-local -> engine-world transform.
        void AttachTerrainComponents(
            const std::vector<Scad::SceneTerrain>& terrains,
            double scale,
            ScadBuildCache& cache)
        {
            if (terrains.empty())
            {
                return;
            }

            // SCAD (Z-up) -> engine (Y-up): world = (x, z, -y) * scale.
            glm::dmat4 scadToEngine(0.0);
            scadToEngine[0] = glm::dvec4(scale, 0.0, 0.0, 0.0);
            scadToEngine[1] = glm::dvec4(0.0, 0.0, -scale, 0.0);
            scadToEngine[2] = glm::dvec4(0.0, scale, 0.0, 0.0);
            scadToEngine[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);

            for (const Scad::SceneTerrain& terrain : terrains)
            {
                if (!terrain.data)
                {
                    continue;
                }
                auto found = cache.nodeByInstanceId.find(terrain.ownerInstanceId);
                if (found == cache.nodeByInstanceId.end())
                {
                    SPDLOG_WARN("SCAD: terrain payload owner node {} not found; skipping TerrainComponent",
                                terrain.ownerInstanceId);
                    continue;
                }
                const std::shared_ptr<Node>& node = found->second.first;
                const glm::dmat4& nodeWorld = found->second.second;

                const Scad::FTerrainData& src = *terrain.data;
                auto grid = std::make_shared<Runtime::TerrainComponent::FGridData>();
                grid->cellsX = src.spec.cells.x;
                grid->cellsY = src.spec.cells.y;
                grid->sizeX = src.spec.size.x;
                grid->sizeY = src.spec.size.y;
                grid->seed = src.spec.seed;
                grid->verts = src.verts;
                grid->diagFlip = src.cellDiagFlip;
                grid->biome = src.cellBiome;
                grid->flags = src.cellFlags;
                grid->waterLevel = src.cellWater;
                grid->localToWorld = nodeWorld * scadToEngine * terrain.xform;
                grid->worldToLocal = glm::inverse(grid->localToWorld);

                auto component = std::make_shared<Runtime::TerrainComponent>();
                component->SetGridData(std::move(grid));
                node->AddComponent(component);
            }
        }
    } // namespace

    namespace
    {
        // ------------------------------------------------------------------
        // Camera markers: gk_camera() / gk_camera_key() are zero-geometry
        // virtual points (see assets/scad/lib/gk_camera.scad). Marker local
        // space: front = +Y, up = +Z (SCAD Z-up); after the Z-up -> Y-up bake
        // this matches the engine camera convention (front = -Z, up = +Y), so
        // fixed and path cameras both reuse the glTF camera runtime path
        // (EnvironmentSetting.cameras + Scene::HasCameraAnimation override).
        // ------------------------------------------------------------------

        const Scad::Value* FindScadParam(const Scad::SceneNode& node, const char* name)
        {
            for (const auto& [key, value] : node.parameters)
            {
                if (key == name)
                {
                    return &value;
                }
            }
            return nullptr;
        }

        std::string ScadParamString(const Scad::SceneNode& node, const char* name, const std::string& fallback)
        {
            const Scad::Value* value = FindScadParam(node, name);
            return (value && value->type == Scad::Value::Type::Str && !value->str.empty()) ? value->str : fallback;
        }

        float ScadParamNumber(const Scad::SceneNode& node, const char* name, float fallback)
        {
            const Scad::Value* value = FindScadParam(node, name);
            return (value && value->IsNumber()) ? static_cast<float>(value->num) : fallback;
        }

        void DecomposeWorldTR(const glm::dmat4& world, glm::vec3& outTranslation, glm::quat& outRotation)
        {
            glm::dvec3 scaleD(1.0);
            glm::dvec3 translationD(0.0);
            glm::dvec3 skew(0.0);
            glm::dvec4 perspective(0.0);
            glm::dquat rotationD(1.0, 0.0, 0.0, 0.0);
            if (glm::decompose(world, scaleD, rotationD, translationD, skew, perspective))
            {
                outTranslation = glm::vec3(translationD);
                outRotation = glm::normalize(glm::quat(rotationD));
            }
            else
            {
                outTranslation = glm::vec3(0.0f);
                outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        glm::mat4 CameraModelViewFromWorld(const glm::dmat4& world)
        {
            glm::vec3 eye(0.0f);
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            DecomposeWorldTR(world, eye, rotation);
            return glm::lookAtRH(eye, eye + rotation * glm::vec3(0.0f, 0.0f, -1.0f),
                                 rotation * glm::vec3(0.0f, 1.0f, 0.0f));
        }

        void CollectScadCameraMarkersRecursive(
            const Scad::SceneNode& sceneNode,
            std::vector<const Scad::SceneNode*>& outFixed,
            std::vector<const Scad::SceneNode*>& outKeys)
        {
            if (sceneNode.name == "gk_camera")
            {
                outFixed.push_back(&sceneNode);
            }
            else if (sceneNode.name == "gk_camera_key")
            {
                outKeys.push_back(&sceneNode);
            }
            for (const Scad::SceneNode& child : sceneNode.children)
            {
                CollectScadCameraMarkersRecursive(child, outFixed, outKeys);
            }
        }

        void BuildScadCameras(
            const std::vector<Scad::SceneNode>& roots,
            ScadBuildCache& cache,
            std::vector<std::shared_ptr<Node>>& nodes,
            EnvironmentSetting& cameraInit,
            std::vector<AnimationTrack>& tracks)
        {
            std::vector<const Scad::SceneNode*> fixedMarkers;
            std::vector<const Scad::SceneNode*> keyMarkers;
            for (const Scad::SceneNode& root : roots)
            {
                CollectScadCameraMarkersRecursive(root, fixedMarkers, keyMarkers);
            }
            if (fixedMarkers.empty() && keyMarkers.empty())
            {
                return;
            }

            std::unordered_set<std::string> usedNodeNames;
            for (const auto& node : nodes)
            {
                usedNodeNames.insert(node->GetName());
            }
            const auto uniqueName = [&usedNodeNames](const std::string& base)
            {
                std::string name = base;
                for (int suffix = 1; usedNodeNames.count(name) != 0; ++suffix)
                {
                    name = fmt::format("{}_{}", base, suffix);
                }
                usedNodeNames.insert(name);
                return name;
            };
            const auto findEntry = [&cache](const Scad::SceneNode& marker)
                -> const std::pair<std::shared_ptr<Node>, glm::dmat4>*
            {
                const auto found = cache.nodeByInstanceId.find(marker.instanceId);
                return found == cache.nodeByInstanceId.end() ? nullptr : &found->second;
            };
            const auto fillCameraParams = [](Camera& camera, const Scad::SceneNode& marker)
            {
                camera.FieldOfView = ScadParamNumber(marker, "fov", 55.0f);
                camera.Aperture = ScadParamNumber(marker, "aperture", 0.0f);
                camera.FocalDistance = ScadParamNumber(marker, "focal", 10.0f);
                if (camera.FocalDistance <= 0.0f)
                {
                    camera.FocalDistance = 10.0f;
                }
            };

            // ---- Fixed cameras: one cameras[] entry per gk_camera marker ----
            for (const Scad::SceneNode* marker : fixedMarkers)
            {
                const auto* entry = findEntry(*marker);
                if (!entry)
                {
                    continue;
                }
                const std::string cameraName = ScadParamString(*marker, "name", "cam");
                const std::string nodeName = uniqueName(fmt::format("cam_{}", cameraName));
                entry->first->SetName(nodeName);

                Camera camera{};
                camera.name = cameraName;
                camera.NodeName_ = nodeName;
                camera.ModelView = CameraModelViewFromWorld(entry->second);
                fillCameraParams(camera, *marker);
                cameraInit.cameras.push_back(std::move(camera));
            }

            // ---- Path cameras: group gk_camera_key markers by path, sort by t ----
            std::map<std::string, std::vector<const Scad::SceneNode*>> keysByPath;
            for (const Scad::SceneNode* marker : keyMarkers)
            {
                keysByPath[ScadParamString(*marker, "path", "fly")].push_back(marker);
            }
            for (auto& [pathName, keys] : keysByPath)
            {
                std::sort(keys.begin(), keys.end(),
                          [](const Scad::SceneNode* a, const Scad::SceneNode* b)
                          { return ScadParamNumber(*a, "t", 0.0f) < ScadParamNumber(*b, "t", 0.0f); });
                if (keys.size() < 2)
                {
                    SPDLOG_WARN("SCAD: camera path '{}' has < 2 keys; skipping animation", pathName);
                    continue;
                }

                const std::string nodeName = uniqueName(fmt::format("campath_{}", pathName));
                AnimationTrack track;
                track.AnimationName = pathName;
                track.NodeName_ = nodeName;

                glm::vec3 firstTranslation(0.0f);
                glm::quat firstRotation(1.0f, 0.0f, 0.0f, 0.0f);
                glm::dmat4 firstWorld(1.0);
                for (size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex)
                {
                    const Scad::SceneNode& marker = *keys[keyIndex];
                    const auto* entry = findEntry(marker);
                    if (!entry)
                    {
                        continue;
                    }
                    // The path node sits at scene root, so keys are baked to world TRS.
                    glm::vec3 translation(0.0f);
                    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
                    DecomposeWorldTR(entry->second, translation, rotation);
                    const float time = std::max(0.0f, ScadParamNumber(marker, "t", 0.0f));
                    track.TranslationChannel.Keys.push_back({time, translation});
                    track.RotationChannel.Keys.push_back({time, rotation});
                    track.Duration_ = std::max(track.Duration_, time);
                    if (track.TranslationChannel.Keys.size() == 1)
                    {
                        firstTranslation = translation;
                        firstRotation = rotation;
                        firstWorld = entry->second;
                    }
                    // Rename the key marker node for outliner readability.
                    entry->first->SetName(uniqueName(fmt::format("camkey_{}_{}", pathName, keyIndex)));
                }
                if (track.Duration_ <= 0.0f)
                {
                    SPDLOG_WARN("SCAD: camera path '{}' has zero duration; skipping animation", pathName);
                    continue;
                }

                auto pathNode = Node::CreateNode(nodeName, firstTranslation, firstRotation, glm::vec3(1.0f),
                                                 static_cast<uint32_t>(nodes.size()));
                nodes.push_back(pathNode);
                tracks.push_back(std::move(track));

                Camera camera{};
                camera.name = pathName;
                camera.NodeName_ = nodeName;
                camera.ModelView = CameraModelViewFromWorld(firstWorld);
                fillCameraParams(camera, *keys.front());
                cameraInit.cameras.push_back(std::move(camera));
            }

            SPDLOG_INFO("SCAD: {} fixed camera(s), {} camera path(s) collected", fixedMarkers.size(),
                        keysByPath.size());
        }
    } // namespace

    bool FScadLoader::LoadScadScene(
        const std::string& filename,
        EnvironmentSetting& cameraInit,
        std::vector<std::shared_ptr<Node>>& nodes,
        std::vector<Model>& models,
        std::vector<FMaterial>& materials,
        std::vector<LightObject>& /*lights*/,
        std::vector<AnimationTrack>& tracks,
        std::vector<Skeleton>& /*skeletons*/,
        const ScadLoadOptions& options)
    {
        // ---- Resolve the use/include closure ----
        Scad::ScadProgram program;
        std::string programErr;
        if (!Scad::LoadScadProgram(filename, program, programErr))
        {
            SPDLOG_ERROR("SCAD: {}", programErr);
            return false;
        }

        // ---- Evaluate ----
        Scad::SceneEvalResult result;
        std::string evalErr;
        Scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions, options, result, evalErr);

        if (result.roots.empty())
        {
            SPDLOG_WARN("SCAD: no geometry produced from {}", filename);
        }

        const double scale = static_cast<double>(options.scadToWorldScale > 0.0f ? options.scadToWorldScale : 1.0f);

        // ---- Recreate the SCAD user-module hierarchy ----
        ScadBuildCache buildCache;
        size_t rootCount = 0;
        for (const Scad::SceneNode& root : result.roots)
        {
            BuildScadSceneNodeRecursive(root, nullptr, glm::dmat4(1.0), scale, options.smoothAngleDegrees, nodes, models, materials, buildCache);
            ++rootCount;
        }

        // ---- Terrain payloads -> TerrainComponent (height/water/biome queries) ----
        AttachTerrainComponents(result.terrains, scale, buildCache);

        // ---- Camera markers -> cameras[] + path animation tracks ----
        BuildScadCameras(result.roots, buildCache, nodes, cameraInit, tracks);

        // ---- Camera + environment ----
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SunRotation = 0.35f;
        cameraInit.SunIntensity = 500.0f;
        cameraInit.SkyIntensity = 80.0f;

        Camera defaultCam = AutoFocusCamera(cameraInit, nodes, models, true);
        if (cameraInit.cameras.empty())
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        SPDLOG_INFO("SCAD: loaded {} -> {} root nodes, {} total nodes, {} triangles ({} warnings)",
                    filename, rootCount, nodes.size(), result.triangleCount, result.warningCount);
        return true;
    }
} // namespace Assets
