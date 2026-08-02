#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SceneExport/FSceneSaver.h"

#include <tiny_gltf.h>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"

#include <numeric>

namespace tinygltf
{
    bool LoadImageData(Image* image, const int imageIdx, std::string* err,
                       std::string* warn, int reqWidth, int reqHeight,
                       const unsigned char* bytes, int size, void* userData)
    {
        image->as_is = true;
        return true;
    }
}

namespace Assets
{
namespace
{
    struct FSceneSaverContext
    {
        tinygltf::Model Gltf;
        tinygltf::Buffer Buffer;
        std::unordered_map<uint32_t, int> MaterialIndexMap;
        std::unordered_map<uint32_t, int> MeshIndexMap;
        std::unordered_map<uint32_t, int> TextureIndexMap;
        std::unordered_map<uint32_t, const Runtime::RenderComponent*> ModelRenderComponents;
        std::unordered_map<const Node*, int> NodeIndexMap;
        std::unordered_map<std::string, int> CameraNodeNameToIndex;
        int FallbackMaterialIndex = -1;
    };

    void AddExtensionUsed(tinygltf::Model& model, const std::string& extension)
    {
        if (std::find(model.extensionsUsed.begin(), model.extensionsUsed.end(), extension) == model.extensionsUsed.end())
        {
            model.extensionsUsed.push_back(extension);
        }
    }

    void AddExtensionRequired(tinygltf::Model& model, const std::string& extension)
    {
        AddExtensionUsed(model, extension);
        if (std::find(model.extensionsRequired.begin(), model.extensionsRequired.end(), extension) == model.extensionsRequired.end())
        {
            model.extensionsRequired.push_back(extension);
        }
    }

    void AlignBuffer(tinygltf::Buffer& buffer, size_t alignment = 4)
    {
        const size_t padding = (alignment - (buffer.data.size() % alignment)) % alignment;
        buffer.data.insert(buffer.data.end(), padding, 0);
    }

    int AppendToBuffer(tinygltf::Buffer& buffer, const void* data, size_t size)
    {
        AlignBuffer(buffer);
        const int offset = static_cast<int>(buffer.data.size());
        const auto* byteData = static_cast<const unsigned char*>(data);
        buffer.data.insert(buffer.data.end(), byteData, byteData + size);
        return offset;
    }

    int CreateBufferView(tinygltf::Model& model, int bufferIdx, size_t offset, size_t length, int target)
    {
        tinygltf::BufferView view;
        view.buffer = bufferIdx;
        view.byteOffset = offset;
        view.byteLength = length;
        if (target != 0)
        {
            view.target = target;
        }
        model.bufferViews.push_back(view);
        return static_cast<int>(model.bufferViews.size() - 1);
    }

    int GltfAccessorType(const std::string& type)
    {
        if (type == "SCALAR")
        {
            return TINYGLTF_TYPE_SCALAR;
        }
        if (type == "VEC2")
        {
            return TINYGLTF_TYPE_VEC2;
        }
        if (type == "VEC3")
        {
            return TINYGLTF_TYPE_VEC3;
        }
        if (type == "VEC4")
        {
            return TINYGLTF_TYPE_VEC4;
        }
        SPDLOG_ERROR("Unknown accessor type: {}", type);
        return TINYGLTF_TYPE_SCALAR;
    }

    int CreateAccessor(tinygltf::Model& model, int bufferViewIdx, int componentType, int count,
                       const std::string& type, const std::vector<double>& min = {},
                       const std::vector<double>& max = {})
    {
        tinygltf::Accessor accessor;
        accessor.bufferView = bufferViewIdx;
        accessor.componentType = componentType;
        accessor.count = count;
        accessor.type = GltfAccessorType(type);
        accessor.minValues = min;
        accessor.maxValues = max;
        model.accessors.push_back(accessor);
        return static_cast<int>(model.accessors.size() - 1);
    }

    bool HasPrefixCameraIndex(const std::string& name)
    {
        const size_t spacePos = name.find(' ');
        if (spacePos == std::string::npos || spacePos == 0)
        {
            return false;
        }

        return std::all_of(name.begin(), name.begin() + static_cast<std::ptrdiff_t>(spacePos), [](char c)
        {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
    }

    std::string CameraNodeName(const Camera& camera)
    {
        if (!camera.NodeName_.empty())
        {
            return camera.NodeName_;
        }
        if (HasPrefixCameraIndex(camera.name))
        {
            return camera.name.substr(camera.name.find(' ') + 1);
        }
        return camera.name;
    }

    void CollectRenderComponents(FSceneSaverContext& ctx, const Scene& scene)
    {
        for (const auto* renderComp : scene.Components<Runtime::RenderComponent>())
        {
            const Node* node = renderComp->GetOwner();
            if (!node)
            {
                continue;
            }
            if (node->IsSceneReferenceInternal())
            {
                continue;
            }
            if (!renderComp->IsDrawable())
            {
                continue;
            }

            const uint32_t modelId = renderComp->GetModelId();
            if (!ctx.ModelRenderComponents.contains(modelId))
            {
                ctx.ModelRenderComponents[modelId] = renderComp;
            }
        }
    }

    tinygltf::Material MakeFallbackMaterial()
    {
        tinygltf::Material material;
        material.name = "Default";
        material.pbrMetallicRoughness.baseColorFactor = {1.0, 1.0, 1.0, 1.0};
        material.pbrMetallicRoughness.metallicFactor = 0.0;
        material.pbrMetallicRoughness.roughnessFactor = 1.0;
        return material;
    }

    int EnsureFallbackMaterial(FSceneSaverContext& ctx)
    {
        if (ctx.FallbackMaterialIndex == -1)
        {
            ctx.Gltf.materials.push_back(MakeFallbackMaterial());
            ctx.FallbackMaterialIndex = static_cast<int>(ctx.Gltf.materials.size() - 1);
        }
        return ctx.FallbackMaterialIndex;
    }

    bool IsTextureIdValid(int32_t textureId)
    {
        return textureId >= 0;
    }

    int ExportTexture(FSceneSaverContext& ctx, int32_t engineTextureId)
    {
        if (!IsTextureIdValid(engineTextureId))
        {
            return -1;
        }

        const uint32_t textureId = static_cast<uint32_t>(engineTextureId);
        if (auto found = ctx.TextureIndexMap.find(textureId); found != ctx.TextureIndexMap.end())
        {
            return found->second;
        }

        const FTextureCpuSource* source = GlobalTexturePool::GetTextureCpuSource(textureId);
        if (!source || source->Bytes.empty())
        {
            SPDLOG_WARN("Texture {} has no retained CPU source; skipping glTF texture export", textureId);
            return -1;
        }

        const int imageOffset = AppendToBuffer(ctx.Buffer, source->Bytes.data(), source->Bytes.size());
        const int imageView = CreateBufferView(ctx.Gltf, 0, imageOffset, source->Bytes.size(), 0);

        tinygltf::Image image;
        image.name = source->TextureName;
        image.mimeType = source->Mime;
        image.bufferView = imageView;
        ctx.Gltf.images.push_back(image);
        const int imageIndex = static_cast<int>(ctx.Gltf.images.size() - 1);

        tinygltf::Texture texture;
        texture.name = source->TextureName;
        texture.source = imageIndex;
        if (source->Mime.find("image/webp") != std::string::npos)
        {
            texture.extensions["EXT_texture_webp"] = tinygltf::Value(tinygltf::Value::Object{
                {"source", tinygltf::Value(imageIndex)}
            });
            AddExtensionRequired(ctx.Gltf, "EXT_texture_webp");
        }

        ctx.Gltf.textures.push_back(texture);
        const int gltfTextureIndex = static_cast<int>(ctx.Gltf.textures.size() - 1);
        ctx.TextureIndexMap[textureId] = gltfTextureIndex;
        return gltfTextureIndex;
    }

    void SerializeMaterials(FSceneSaverContext& ctx, const Scene& scene)
    {
        const auto& materials = scene.Materials();
        for (uint32_t materialIdx = 0; materialIdx < materials.size(); ++materialIdx)
        {
            const FMaterial& material = materials[materialIdx];
            const Material& gpuMat = material.gpuMaterial_;

            tinygltf::Material gltfMat;
            gltfMat.name = material.name_;

            if (gpuMat.MaterialModel == Material::Enum::DiffuseLight)
            {
                const glm::vec3 emissive = glm::max(glm::vec3(gpuMat.Diffuse), glm::vec3(0.0f));
                const float strength = std::max(1.0f, std::max(emissive.r, std::max(emissive.g, emissive.b)));
                const glm::vec3 emissiveFactor = emissive / strength;
                gltfMat.pbrMetallicRoughness.baseColorFactor = {1.0, 1.0, 1.0, gpuMat.Diffuse.a};
                gltfMat.emissiveFactor = {emissiveFactor.r, emissiveFactor.g, emissiveFactor.b};
                if (strength > 1.0f)
                {
                    gltfMat.extensions["KHR_materials_emissive_strength"] = tinygltf::Value(tinygltf::Value::Object{
                        {"emissiveStrength", tinygltf::Value(static_cast<double>(strength / 50.0f))}
                    });
                    AddExtensionUsed(ctx.Gltf, "KHR_materials_emissive_strength");
                }
            }
            else
            {
                gltfMat.pbrMetallicRoughness.baseColorFactor = {
                    gpuMat.Diffuse.r * gpuMat.Diffuse.r,
                    gpuMat.Diffuse.g * gpuMat.Diffuse.g,
                    gpuMat.Diffuse.b * gpuMat.Diffuse.b,
                    gpuMat.Diffuse.a
                };
            }

            gltfMat.pbrMetallicRoughness.metallicFactor = gpuMat.Metalness;
            gltfMat.pbrMetallicRoughness.roughnessFactor = gpuMat.Fuzziness;
            gltfMat.pbrMetallicRoughness.baseColorTexture.index = ExportTexture(ctx, gpuMat.DiffuseTextureId);
            gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index = ExportTexture(ctx, gpuMat.MRATextureId);
            gltfMat.normalTexture.index = ExportTexture(ctx, gpuMat.NormalTextureId);
            gltfMat.emissiveTexture.index = ExportTexture(ctx, gpuMat.EmissiveTextureId);
            gltfMat.normalTexture.scale = gpuMat.NormalTextureScale;
            if (gpuMat.Diffuse.a < 0.999f)
            {
                gltfMat.alphaMode = "BLEND";
            }

            if (std::abs(gpuMat.RefractionIndex - 1.46f) > 0.01f)
            {
                gltfMat.extensions["KHR_materials_ior"] = tinygltf::Value(tinygltf::Value::Object{
                    {"ior", tinygltf::Value(static_cast<double>(gpuMat.RefractionIndex))}
                });
                AddExtensionUsed(ctx.Gltf, "KHR_materials_ior");
            }

            if (gpuMat.MaterialModel == Material::Enum::Dielectric)
            {
                gltfMat.extensions["KHR_materials_transmission"] = tinygltf::Value(tinygltf::Value::Object{
                    {"transmissionFactor", tinygltf::Value(1.0)}
                });
                AddExtensionUsed(ctx.Gltf, "KHR_materials_transmission");
            }

            if (std::abs(gpuMat.RefractionIndex2 - gpuMat.RefractionIndex) > 0.001f)
            {
                gltfMat.extras = tinygltf::Value(tinygltf::Value::Object{
                    {"ior2", tinygltf::Value(static_cast<double>(gpuMat.RefractionIndex2))}
                });
            }

            ctx.Gltf.materials.push_back(gltfMat);
            ctx.MaterialIndexMap[materialIdx] = static_cast<int>(ctx.Gltf.materials.size() - 1);
        }

        if (ctx.Gltf.materials.empty())
        {
            EnsureFallbackMaterial(ctx);
        }
    }

    int GltfMaterialIndexForSection(FSceneSaverContext& ctx, const Runtime::RenderComponent* renderComp, uint32_t section)
    {
        if (renderComp)
        {
            const auto& materials = renderComp->GetMaterials();
            const uint32_t materialIdx = section < materials.size() ? materials[section] : materials[0];
            if (auto found = ctx.MaterialIndexMap.find(materialIdx); found != ctx.MaterialIndexMap.end())
            {
                return found->second;
            }
        }

        return EnsureFallbackMaterial(ctx);
    }

    tinygltf::Value SerializeEnvironmentExtras(const EnvironmentSetting& env);

    struct FSectionGeometry
    {
        std::vector<Vertex> Vertices;
        std::vector<uint32_t> Indices;
    };

    uint32_t SectionCountForModel(const Model& model)
    {
        uint32_t sectionCount = std::max<uint32_t>(1, model.SectionCount());
        for (const Vertex& vertex : model.CPUVertices())
        {
            sectionCount = std::max(sectionCount, vertex.MaterialIndex + 1);
        }
        return sectionCount;
    }

    std::vector<FSectionGeometry> SplitModelBySection(const Model& model)
    {
        const uint32_t sectionCount = SectionCountForModel(model);
        std::vector<FSectionGeometry> sections(sectionCount);
        std::vector<std::unordered_map<uint32_t, uint32_t>> remaps(sectionCount);

        std::vector<uint32_t> sourceIndices = model.CPUIndices();
        if (sourceIndices.empty())
        {
            sourceIndices.resize(model.CPUVertices().size());
            std::iota(sourceIndices.begin(), sourceIndices.end(), 0u);
        }

        auto appendIndex = [&](uint32_t sourceIndex, uint32_t section)
        {
            auto& remap = remaps[section];
            auto& geometry = sections[section];
            auto found = remap.find(sourceIndex);
            if (found != remap.end())
            {
                geometry.Indices.push_back(found->second);
                return;
            }

            const uint32_t localIndex = static_cast<uint32_t>(geometry.Vertices.size());
            Vertex vertex = model.CPUVertices()[sourceIndex];
            vertex.MaterialIndex = section;
            geometry.Vertices.push_back(vertex);
            geometry.Indices.push_back(localIndex);
            remap[sourceIndex] = localIndex;
        };

        for (size_t index = 0; index + 2 < sourceIndices.size(); index += 3)
        {
            const uint32_t i0 = sourceIndices[index];
            const uint32_t i1 = sourceIndices[index + 1];
            const uint32_t i2 = sourceIndices[index + 2];
            if (i0 >= model.CPUVertices().size() || i1 >= model.CPUVertices().size() || i2 >= model.CPUVertices().size())
            {
                continue;
            }

            uint32_t section = model.CPUVertices()[i0].MaterialIndex;
            if (section >= sections.size())
            {
                section = 0;
            }

            appendIndex(i0, section);
            appendIndex(i1, section);
            appendIndex(i2, section);
        }

        return sections;
    }

    void AddVertexAttributes(FSceneSaverContext& ctx, tinygltf::Primitive& primitive, const std::vector<Vertex>& vertices)
    {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texCoords;
        std::vector<float> tangents;
        positions.reserve(vertices.size() * 3);
        normals.reserve(vertices.size() * 3);
        texCoords.reserve(vertices.size() * 2);
        tangents.reserve(vertices.size() * 4);

        glm::vec3 minPos(FLT_MAX);
        glm::vec3 maxPos(-FLT_MAX);
        for (const Vertex& vertex : vertices)
        {
            positions.push_back(vertex.Position.x);
            positions.push_back(vertex.Position.y);
            positions.push_back(vertex.Position.z);
            normals.push_back(vertex.Normal.x);
            normals.push_back(vertex.Normal.y);
            normals.push_back(vertex.Normal.z);
            texCoords.push_back(vertex.TexCoord.x);
            texCoords.push_back(vertex.TexCoord.y);
            tangents.push_back(vertex.Tangent.x);
            tangents.push_back(vertex.Tangent.y);
            tangents.push_back(vertex.Tangent.z);
            tangents.push_back(vertex.Tangent.w);
            minPos = glm::min(minPos, vertex.Position);
            maxPos = glm::max(maxPos, vertex.Position);
        }

        const int posOffset = AppendToBuffer(ctx.Buffer, positions.data(), positions.size() * sizeof(float));
        const int posView = CreateBufferView(ctx.Gltf, 0, posOffset, positions.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
        primitive.attributes["POSITION"] = CreateAccessor(ctx.Gltf, posView, TINYGLTF_COMPONENT_TYPE_FLOAT,
            static_cast<int>(vertices.size()), "VEC3", {minPos.x, minPos.y, minPos.z}, {maxPos.x, maxPos.y, maxPos.z});

        const int normalOffset = AppendToBuffer(ctx.Buffer, normals.data(), normals.size() * sizeof(float));
        const int normalView = CreateBufferView(ctx.Gltf, 0, normalOffset, normals.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
        primitive.attributes["NORMAL"] = CreateAccessor(ctx.Gltf, normalView, TINYGLTF_COMPONENT_TYPE_FLOAT,
            static_cast<int>(vertices.size()), "VEC3");

        const int uvOffset = AppendToBuffer(ctx.Buffer, texCoords.data(), texCoords.size() * sizeof(float));
        const int uvView = CreateBufferView(ctx.Gltf, 0, uvOffset, texCoords.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
        primitive.attributes["TEXCOORD_0"] = CreateAccessor(ctx.Gltf, uvView, TINYGLTF_COMPONENT_TYPE_FLOAT,
            static_cast<int>(vertices.size()), "VEC2");

        const int tangentOffset = AppendToBuffer(ctx.Buffer, tangents.data(), tangents.size() * sizeof(float));
        const int tangentView = CreateBufferView(ctx.Gltf, 0, tangentOffset, tangents.size() * sizeof(float), TINYGLTF_TARGET_ARRAY_BUFFER);
        primitive.attributes["TANGENT"] = CreateAccessor(ctx.Gltf, tangentView, TINYGLTF_COMPONENT_TYPE_FLOAT,
            static_cast<int>(vertices.size()), "VEC4");
    }

    void SerializeMeshes(FSceneSaverContext& ctx, const Scene& scene)
    {
        const auto& models = scene.Models();
        for (uint32_t modelIdx = 0; modelIdx < models.size(); ++modelIdx)
        {
            if (!ctx.ModelRenderComponents.contains(modelIdx))
            {
                continue;
            }

            const Model& assetModel = models[modelIdx];
            if (assetModel.CPUVertices().empty())
            {
                SPDLOG_WARN("Model {} has no vertices, skipping", assetModel.Name());
                continue;
            }

            tinygltf::Mesh mesh;
            mesh.name = assetModel.Name();

            const Runtime::RenderComponent* renderComp = nullptr;
            if (auto found = ctx.ModelRenderComponents.find(modelIdx); found != ctx.ModelRenderComponents.end())
            {
                renderComp = found->second;
            }

            std::vector<FSectionGeometry> sections = SplitModelBySection(assetModel);
            for (uint32_t section = 0; section < sections.size(); ++section)
            {
                const FSectionGeometry& geometry = sections[section];
                if (geometry.Vertices.empty() || geometry.Indices.empty())
                {
                    continue;
                }

                tinygltf::Primitive primitive;
                primitive.mode = TINYGLTF_MODE_TRIANGLES;
                AddVertexAttributes(ctx, primitive, geometry.Vertices);

                const int indexOffset = AppendToBuffer(ctx.Buffer, geometry.Indices.data(), geometry.Indices.size() * sizeof(uint32_t));
                const int indexView = CreateBufferView(ctx.Gltf, 0, indexOffset, geometry.Indices.size() * sizeof(uint32_t),
                    TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
                primitive.indices = CreateAccessor(ctx.Gltf, indexView, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                    static_cast<int>(geometry.Indices.size()), "SCALAR");
                primitive.material = GltfMaterialIndexForSection(ctx, renderComp, section);
                mesh.primitives.push_back(primitive);
            }

            if (!mesh.primitives.empty())
            {
                ctx.Gltf.meshes.push_back(mesh);
                ctx.MeshIndexMap[modelIdx] = static_cast<int>(ctx.Gltf.meshes.size() - 1);
            }
        }
    }

    void SerializeCameras(FSceneSaverContext& ctx, const Scene& scene)
    {
        const auto& cameras = scene.GetEnvSettings().cameras;
        for (const Camera& camera : cameras)
        {
            tinygltf::Camera gltfCamera;
            gltfCamera.name = camera.name;
            gltfCamera.type = "perspective";
            gltfCamera.perspective.aspectRatio = 0.0;
            gltfCamera.perspective.yfov = glm::radians(camera.FieldOfView);
            gltfCamera.perspective.znear = camera.NearPlane;
            gltfCamera.perspective.zfar = camera.FarPlane;

            tinygltf::Value::Object extras;
            if (camera.Aperture > 0.0001f)
            {
                extras["F-Stop"] = tinygltf::Value(static_cast<double>(0.2f / camera.Aperture));
            }
            extras["FocalDistance"] = tinygltf::Value(static_cast<double>(camera.FocalDistance));
            if (!extras.empty())
            {
                gltfCamera.extras = tinygltf::Value(extras);
            }

            ctx.Gltf.cameras.push_back(gltfCamera);
            const int cameraIndex = static_cast<int>(ctx.Gltf.cameras.size() - 1);
            const std::string nodeName = CameraNodeName(camera);
            if (!nodeName.empty() && !ctx.CameraNodeNameToIndex.contains(nodeName))
            {
                ctx.CameraNodeNameToIndex[nodeName] = cameraIndex;
            }
        }
    }

    void SerializeNodes(FSceneSaverContext& ctx, const Scene& scene)
    {
        const auto& nodes = scene.Nodes();
        for (size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx)
        {
            const auto& node = nodes[nodeIdx];
            if (node->IsSceneReferenceInternal())
            {
                continue;
            }

            auto sceneReference = node->GetComponent<Runtime::SceneReferenceComponent>();
            tinygltf::Node gltfNode;
            gltfNode.name = node->GetName();

            const glm::vec3 translation = node->Translation();
            const glm::quat rotation = node->Rotation();
            const glm::vec3 scale = node->Scale();
            gltfNode.translation = {translation.x, translation.y, translation.z};
            gltfNode.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
            gltfNode.scale = {scale.x, scale.y, scale.z};

            if (!sceneReference)
            {
                if (auto cameraIt = ctx.CameraNodeNameToIndex.find(node->GetName());
                    cameraIt != ctx.CameraNodeNameToIndex.end())
                {
                    gltfNode.camera = cameraIt->second;
                }

                auto renderComp = node->GetComponent<Runtime::RenderComponent>();
                if (renderComp && renderComp->IsDrawable())
                {
                    if (auto meshIt = ctx.MeshIndexMap.find(renderComp->GetModelId()); meshIt != ctx.MeshIndexMap.end())
                    {
                        gltfNode.mesh = meshIt->second;
                    }
                }
            }
            auto renderComp = sceneReference ? nullptr : node->GetComponent<Runtime::RenderComponent>();

            tinygltf::Value::Object extras;
            extras["tag"] = tinygltf::Value(node->GetTag());
            extras["layer"] = tinygltf::Value(node->GetLayer());
            if (auto environment = node->GetComponent<Runtime::EnvironmentComponent>())
            {
                extras["gkEnvironment"] = SerializeEnvironmentExtras(*environment);
            }
            if (sceneReference)
            {
                extras["gkSceneReference"] = tinygltf::Value(tinygltf::Value::Object{
                    {"version", tinygltf::Value(1)},
                    {"asset", tinygltf::Value(sceneReference->GetAssetPath())},
                });
            }
            else if (renderComp)
            {
                extras["visible"] = tinygltf::Value(renderComp->GetVisible());
                extras["castShadows"] = tinygltf::Value(renderComp->GetCastShadows());
                extras["receiveGI"] = tinygltf::Value(renderComp->GetReceiveGI());
                extras["layerMask"] = tinygltf::Value(static_cast<double>(renderComp->GetLayerMask()));
            }
            gltfNode.extras = tinygltf::Value(extras);

            ctx.Gltf.nodes.push_back(gltfNode);
            ctx.NodeIndexMap[node.get()] = static_cast<int>(ctx.Gltf.nodes.size() - 1);
        }

        for (size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx)
        {
            const auto& node = nodes[nodeIdx];
            if (node->IsSceneReferenceInternal())
            {
                continue;
            }
            auto parentIt = ctx.NodeIndexMap.find(node.get());
            if (parentIt == ctx.NodeIndexMap.end())
            {
                continue;
            }
            if (node->GetComponent<Runtime::SceneReferenceComponent>())
            {
                continue;
            }

            for (const auto& child : node->Children())
            {
                if (auto found = ctx.NodeIndexMap.find(child.get()); found != ctx.NodeIndexMap.end())
                {
                    ctx.Gltf.nodes[parentIt->second].children.push_back(found->second);
                }
            }
        }
    }

    tinygltf::Value SerializeEnvironmentExtras(const EnvironmentSetting& env)
    {
        const auto serializeColor = [](const glm::vec3& color)
        {
            return tinygltf::Value(tinygltf::Value::Array{
                tinygltf::Value(static_cast<double>(color.r)),
                tinygltf::Value(static_cast<double>(color.g)),
                tinygltf::Value(static_cast<double>(color.b)),
            });
        };
        tinygltf::Value::Object extras;
        extras["SkyIdx"] = tinygltf::Value(static_cast<int>(env.SkyIdx));
        extras["SkyIntensity"] = tinygltf::Value(static_cast<double>(env.SkyIntensity));
        extras["SkyRotation"] = tinygltf::Value(static_cast<double>(env.SkyRotation));
        extras["SkyColor"] = serializeColor(env.SkyColor);
        extras["SunIntensity"] = tinygltf::Value(static_cast<double>(env.SunIntensity));
        extras["SunRotation"] = tinygltf::Value(static_cast<double>(env.SunRotation));
        extras["SunElevation"] = tinygltf::Value(static_cast<double>(env.SunElevation));
        extras["SunColor"] = serializeColor(env.SunColor);
        extras["HasSky"] = tinygltf::Value(env.HasSky);
        extras["HasSun"] = tinygltf::Value(env.HasSun);
        extras["ControlSpeed"] = tinygltf::Value(static_cast<double>(env.ControlSpeed));
        extras["GammaCorrection"] = tinygltf::Value(env.GammaCorrection);
        extras["AtmosphereEnabled"] = tinygltf::Value(env.AtmosphereEnabled);
        extras["AerialPerspectiveEnabled"] = tinygltf::Value(env.AerialPerspectiveEnabled);
        extras["HeightFogEnabled"] = tinygltf::Value(env.HeightFogEnabled);

        const auto& atmosphere = env.Atmosphere;
        extras["RayleighScattering"] = serializeColor(atmosphere.RayleighScattering);
        extras["RayleighDensityH"] =
            tinygltf::Value(static_cast<double>(atmosphere.RayleighDensityH));
        extras["MieScattering"] = serializeColor(atmosphere.MieScattering);
        extras["MieDensityH"] =
            tinygltf::Value(static_cast<double>(atmosphere.MieDensityH));
        extras["MieAbsorption"] = serializeColor(atmosphere.MieAbsorption);
        extras["MiePhaseG"] = tinygltf::Value(static_cast<double>(atmosphere.MiePhaseG));
        extras["OzoneAbsorption"] = serializeColor(atmosphere.OzoneAbsorption);
        extras["OzoneCenterAltitude"] =
            tinygltf::Value(static_cast<double>(atmosphere.OzoneCenterAltitude));
        extras["OzoneWidth"] = tinygltf::Value(static_cast<double>(atmosphere.OzoneWidth));
        extras["GroundAlbedo"] = serializeColor(atmosphere.GroundAlbedo);
        extras["BottomRadius"] = tinygltf::Value(static_cast<double>(atmosphere.BottomRadius));
        extras["TopRadius"] = tinygltf::Value(static_cast<double>(atmosphere.TopRadius));
        extras["WorldUnitsPerKm"] =
            tinygltf::Value(static_cast<double>(atmosphere.WorldUnitsPerKm));
        extras["WorldOriginAltitude"] =
            tinygltf::Value(static_cast<double>(atmosphere.WorldOriginAltitude));
        extras["AerialPerspectiveMaxDistance"] =
            tinygltf::Value(static_cast<double>(atmosphere.AerialPerspectiveMaxDistance));
        extras["SkyLuminanceScale"] =
            tinygltf::Value(static_cast<double>(atmosphere.SkyLuminanceScale));
        extras["FogInscatteringColor"] = serializeColor(atmosphere.FogInscatteringColor);
        extras["FogDensity"] = tinygltf::Value(static_cast<double>(atmosphere.FogDensity));
        extras["FogHeightFalloff"] =
            tinygltf::Value(static_cast<double>(atmosphere.FogHeightFalloff));
        extras["FogBaseHeight"] =
            tinygltf::Value(static_cast<double>(atmosphere.FogBaseHeight));
        extras["FogStartDistance"] =
            tinygltf::Value(static_cast<double>(atmosphere.FogStartDistance));
        extras["FogMaxOpacity"] =
            tinygltf::Value(static_cast<double>(atmosphere.FogMaxOpacity));
        extras["WithSun"] = tinygltf::Value(env.HasSun ? 1 : 0);
        extras["CamSpeed"] = tinygltf::Value(static_cast<double>(env.ControlSpeed));
        if (!env.HasSky)
        {
            extras["NoSky"] = tinygltf::Value(1);
        }
        return tinygltf::Value(extras);
    }

    void SerializeSceneObject(FSceneSaverContext& ctx, const Scene& scene)
    {
        tinygltf::Scene gltfScene;
        gltfScene.name = "Scene";
        for (const auto& node : scene.Nodes())
        {
            if (node->GetParent() == nullptr)
            {
                if (auto found = ctx.NodeIndexMap.find(node.get()); found != ctx.NodeIndexMap.end())
                {
                    gltfScene.nodes.push_back(found->second);
                }
            }
        }
        ctx.Gltf.scenes.push_back(gltfScene);
        ctx.Gltf.defaultScene = 0;
    }

    bool SerializeSceneToGltf(tinygltf::Model& outModel, const Scene& scene)
    {
        FSceneSaverContext ctx;
        ctx.Gltf.asset.version = "2.0";
        ctx.Gltf.asset.generator = "gkNextRenderer Scene Saver";
        ctx.Buffer.name = "SceneBuffer";

        CollectRenderComponents(ctx, scene);
        SerializeMaterials(ctx, scene);
        SerializeMeshes(ctx, scene);
        SerializeCameras(ctx, scene);
        SerializeNodes(ctx, scene);
        SerializeSceneObject(ctx, scene);

        ctx.Gltf.buffers.push_back(std::move(ctx.Buffer));
        outModel = std::move(ctx.Gltf);
        return true;
    }
}

bool FSceneSaver::SaveGLBScene(const std::string& filename, const Scene& scene)
{
    try
    {
        if (scene.Nodes().empty())
        {
            SPDLOG_ERROR("Cannot save empty scene");
            return false;
        }

        tinygltf::Model model;
        if (!SerializeScene(model, scene))
        {
            SPDLOG_ERROR("Failed to serialize scene");
            return false;
        }

        tinygltf::TinyGLTF gltfWriter;
        std::string err;
        std::string warn;

        const bool success = gltfWriter.WriteGltfSceneToFile(&model, filename, true, true, false, true);
        if (!success)
        {
            SPDLOG_ERROR("Failed to write GLB file: {}", err);
            return false;
        }
        if (!warn.empty())
        {
            SPDLOG_WARN("GLB write warnings: {}", warn);
        }

        SPDLOG_INFO("Successfully saved scene to: {}", filename);
        return true;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("Exception during scene save: {}", e.what());
        return false;
    }
}

bool FSceneSaver::SaveGLTFScene(const std::string& filename, const Scene& scene)
{
    try
    {
        if (scene.Nodes().empty())
        {
            SPDLOG_ERROR("Cannot save empty scene");
            return false;
        }

        tinygltf::Model model;
        if (!SerializeScene(model, scene))
        {
            SPDLOG_ERROR("Failed to serialize scene");
            return false;
        }

        tinygltf::TinyGLTF gltfWriter;
        std::string err;
        std::string warn;

        const bool success = gltfWriter.WriteGltfSceneToFile(&model, filename, true, true, true, false);
        if (!success)
        {
            SPDLOG_ERROR("Failed to write GLTF file: {}", err);
            return false;
        }
        if (!warn.empty())
        {
            SPDLOG_WARN("GLTF write warnings: {}", warn);
        }

        SPDLOG_INFO("Successfully saved scene to: {}", filename);
        return true;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("Exception during scene save: {}", e.what());
        return false;
    }
}

bool FSceneSaver::SerializeScene(tinygltf::Model& outModel, const Scene& scene)
{
    return SerializeSceneToGltf(outModel, scene);
}

void FSceneSaver::SerializeNodes(tinygltf::Model& model, const Scene& scene)
{
    SPDLOG_WARN("FSceneSaver::SerializeNodes is deprecated; use SerializeScene");
}

void FSceneSaver::SerializeMeshes(tinygltf::Model& model, const Scene& scene)
{
    SPDLOG_WARN("FSceneSaver::SerializeMeshes is deprecated; use SerializeScene");
}

void FSceneSaver::SerializeMaterials(tinygltf::Model& model, const Scene& scene)
{
    SPDLOG_WARN("FSceneSaver::SerializeMaterials is deprecated; use SerializeScene");
}

void FSceneSaver::SerializeCameras(tinygltf::Model& model, const Scene& scene)
{
    SPDLOG_WARN("FSceneSaver::SerializeCameras is deprecated; use SerializeScene");
}

void FSceneSaver::CopyTextures(tinygltf::Model& outModel, const tinygltf::Model& sourceModel)
{
    SPDLOG_WARN("Texture copying from source glTF is deprecated; scene textures are exported from runtime sources");
}

int FSceneSaver::AppendToBuffer(tinygltf::Buffer& buffer, const void* data, size_t size)
{
    return Assets::AppendToBuffer(buffer, data, size);
}

int FSceneSaver::CreateBufferView(tinygltf::Model& model, int bufferIdx, size_t offset, size_t length, int target)
{
    return Assets::CreateBufferView(model, bufferIdx, offset, length, target);
}

int FSceneSaver::CreateAccessor(tinygltf::Model& model, int bufferViewIdx, int componentType, int count,
                                const std::string& type, const std::vector<double>& min,
                                const std::vector<double>& max)
{
    return Assets::CreateAccessor(model, bufferViewIdx, componentType, count, type, min, max);
}
}

namespace SceneExport
{
    bool SaveScene(const Assets::Scene& scene, const std::string& path)
    {
        if (path.ends_with(".glb") || path.ends_with(".GLB"))
        {
            return Assets::FSceneSaver::SaveGLBScene(path, scene);
        }
        if (path.ends_with(".gltf") || path.ends_with(".GLTF"))
        {
            return Assets::FSceneSaver::SaveGLTFScene(path, scene);
        }
        SPDLOG_ERROR("Unsupported file extension. Use .glb or .gltf");
        return false;
    }
}
