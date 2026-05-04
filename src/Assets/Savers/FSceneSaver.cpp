#include "Common/CoreMinimal.hpp"
#include "Assets/Savers/FSceneSaver.h"

#include <tiny_gltf.h>
#include <spdlog/spdlog.h>
#include <map>

#include "Assets/Core/Scene.hpp"
#include "Assets/Core/Node.h"
#include "Assets/Core/Model.hpp"
#include "Assets/Data/Material.hpp"
#include "Runtime/Components/RenderComponent.h"

// Dummy image loader for TinyGLTF (we don't load images when saving)
// This must be in global namespace for TinyGLTF to find it
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

bool FSceneSaver::SaveGLBScene(const std::string& filename, const Scene& scene)
{
    try
    {
        // 验证Scene数据
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
        std::string err, warn;

        bool success = gltfWriter.WriteGltfSceneToFile(&model, filename,
            true,  // embedImages
            true,  // embedBuffers
            false, // prettyPrint
            true); // writeBinary

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
        // 验证Scene数据
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
        std::string err, warn;

        bool success = gltfWriter.WriteGltfSceneToFile(&model, filename,
            false, // embedImages
            false, // embedBuffers
            true,  // prettyPrint
            false); // writeBinary

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
    // 设置GLTF元数据
    outModel.asset.version = "2.0";
    outModel.asset.generator = "gkNextRenderer Scene Saver";

    // 序列化各个部分
    SerializeMeshes(outModel, scene);
    SerializeMaterials(outModel, scene);
    SerializeCameras(outModel, scene);
    SerializeNodes(outModel, scene);

    return true;
}

void FSceneSaver::SerializeNodes(tinygltf::Model& model, const Scene& scene)
{
    const auto& nodes = scene.Nodes();
    const auto& cameras = scene.GetCameras();

    // 建立Node名称到Camera索引的映射
    // Camera的name格式是"<gltf_camera_index> <node_name>"
    std::map<std::string, int> nodeToCameraIdx;
    for (size_t camIdx = 0; camIdx < cameras.size(); ++camIdx)
    {
        const std::string& camName = cameras[camIdx].name;
        // 从Camera名称中提取Node名称（跳过前面的索引部分）
        size_t spacePos = camName.find(' ');
        if (spacePos != std::string::npos && spacePos + 1 < camName.size())
        {
            std::string nodeName = camName.substr(spacePos + 1);
            nodeToCameraIdx[nodeName] = static_cast<int>(camIdx);
        }
    }

    // 建立Node指针到索引的映射
    std::map<const Node*, int> nodeIndexMap;

    // 第一遍：创建所有tinygltf::Node
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& node = nodes[i];
        tinygltf::Node gltfNode;
        gltfNode.name = node->GetName();

        // 使用TRS形式（优先于矩阵）
        glm::vec3 t = node->Translation();
        glm::quat r = node->Rotation();
        glm::vec3 s = node->Scale();

        gltfNode.translation = {t.x, t.y, t.z};
        // GLTF四元数顺序是[x,y,z,w]，GLM是[w,x,y,z]
        gltfNode.rotation = {r.x, r.y, r.z, r.w};
        gltfNode.scale = {s.x, s.y, s.z};

        // 关联Camera（如果有）
        auto it = nodeToCameraIdx.find(node->GetName());
        if (it != nodeToCameraIdx.end())
        {
            gltfNode.camera = it->second;
        }

        // 关联Mesh（如果有RenderComponent）
        auto renderComp = node->GetComponent<Runtime::RenderComponent>();
        if (renderComp && renderComp->IsDrawable())
        {
            gltfNode.mesh = renderComp->GetModelId();
        }

        model.nodes.push_back(gltfNode);
        nodeIndexMap[node.get()] = static_cast<int>(i);
    }

    // 第二遍：建立父子关系
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        for (const auto& child : nodes[i]->Children())
        {
            int childIdx = nodeIndexMap[child.get()];
            model.nodes[i].children.push_back(childIdx);
        }
    }

    // 创建默认Scene，添加根节点
    tinygltf::Scene gltfScene;
    gltfScene.name = "Scene";
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i]->GetParent() == nullptr)
        {
            gltfScene.nodes.push_back(static_cast<int>(i));
        }
    }
    model.scenes.push_back(gltfScene);
    model.defaultScene = 0;
}

void FSceneSaver::SerializeMeshes(tinygltf::Model& model, const Scene& scene)
{
    // 创建全局Buffer
    tinygltf::Buffer buffer;
    buffer.name = "SceneBuffer";

    const auto& models = scene.Models();
    const auto& nodes = scene.Nodes();

    // 为每个Model找到第一个使用它的Node，以获取材质信息
    std::map<uint32_t, Runtime::RenderComponent*> modelToRenderComp;
    for (const auto& node : nodes)
    {
        auto renderComp = node->GetComponent<Runtime::RenderComponent>();
        if (renderComp && renderComp->IsDrawable())
        {
            uint32_t modelId = renderComp->GetModelId();
            if (modelToRenderComp.find(modelId) == modelToRenderComp.end())
            {
                modelToRenderComp[modelId] = renderComp.get();
            }
        }
    }

    for (uint32_t modelIdx = 0; modelIdx < models.size(); ++modelIdx)
    {
        const auto& assetModel = models[modelIdx];
        tinygltf::Mesh mesh;
        mesh.name = assetModel.Name();

        // 第一版简化处理：一个Model = 一个Primitive
        tinygltf::Primitive primitive;
        primitive.mode = TINYGLTF_MODE_TRIANGLES;

        const auto& vertices = assetModel.CPUVertices();
        if (vertices.empty())
        {
            SPDLOG_WARN("Model {} has no vertices, skipping", assetModel.Name());
            continue;
        }

        // === 写入Position数据 ===
        std::vector<float> positions;
        positions.reserve(vertices.size() * 3);

        glm::vec3 minPos(FLT_MAX), maxPos(-FLT_MAX);
        for (const auto& v : vertices)
        {
            positions.push_back(v.Position.x);
            positions.push_back(v.Position.y);
            positions.push_back(v.Position.z);
            minPos = glm::min(minPos, v.Position);
            maxPos = glm::max(maxPos, v.Position);
        }

        size_t posOffset = buffer.data.size();
        size_t posSize = positions.size() * sizeof(float);
        buffer.data.insert(buffer.data.end(),
            reinterpret_cast<const unsigned char*>(positions.data()),
            reinterpret_cast<const unsigned char*>(positions.data()) + posSize);

        int posBufferView = CreateBufferView(model, 0, posOffset, posSize, TINYGLTF_TARGET_ARRAY_BUFFER);
        int posAccessor = CreateAccessor(model, posBufferView,
            TINYGLTF_COMPONENT_TYPE_FLOAT, static_cast<int>(vertices.size()), "VEC3",
            {minPos.x, minPos.y, minPos.z}, {maxPos.x, maxPos.y, maxPos.z});
        primitive.attributes["POSITION"] = posAccessor;

        // === 写入Normal数据 ===
        std::vector<float> normals;
        normals.reserve(vertices.size() * 3);
        for (const auto& v : vertices)
        {
            normals.push_back(v.Normal.x);
            normals.push_back(v.Normal.y);
            normals.push_back(v.Normal.z);
        }

        size_t normalOffset = buffer.data.size();
        size_t normalSize = normals.size() * sizeof(float);
        buffer.data.insert(buffer.data.end(),
            reinterpret_cast<const unsigned char*>(normals.data()),
            reinterpret_cast<const unsigned char*>(normals.data()) + normalSize);

        int normalBufferView = CreateBufferView(model, 0, normalOffset, normalSize, TINYGLTF_TARGET_ARRAY_BUFFER);
        int normalAccessor = CreateAccessor(model, normalBufferView,
            TINYGLTF_COMPONENT_TYPE_FLOAT, static_cast<int>(vertices.size()), "VEC3");
        primitive.attributes["NORMAL"] = normalAccessor;

        // === 写入TexCoord数据 ===
        std::vector<float> texCoords;
        texCoords.reserve(vertices.size() * 2);
        for (const auto& v : vertices)
        {
            texCoords.push_back(v.TexCoord.x);
            texCoords.push_back(v.TexCoord.y);
        }

        size_t texCoordOffset = buffer.data.size();
        size_t texCoordSize = texCoords.size() * sizeof(float);
        buffer.data.insert(buffer.data.end(),
            reinterpret_cast<const unsigned char*>(texCoords.data()),
            reinterpret_cast<const unsigned char*>(texCoords.data()) + texCoordSize);

        int texCoordBufferView = CreateBufferView(model, 0, texCoordOffset, texCoordSize, TINYGLTF_TARGET_ARRAY_BUFFER);
        int texCoordAccessor = CreateAccessor(model, texCoordBufferView,
            TINYGLTF_COMPONENT_TYPE_FLOAT, static_cast<int>(vertices.size()), "VEC2");
        primitive.attributes["TEXCOORD_0"] = texCoordAccessor;

        // === 写入Tangent数据 ===
        std::vector<float> tangents;
        tangents.reserve(vertices.size() * 4);
        for (const auto& v : vertices)
        {
            tangents.push_back(v.Tangent.x);
            tangents.push_back(v.Tangent.y);
            tangents.push_back(v.Tangent.z);
            tangents.push_back(v.Tangent.w);
        }

        size_t tangentOffset = buffer.data.size();
        size_t tangentSize = tangents.size() * sizeof(float);
        buffer.data.insert(buffer.data.end(),
            reinterpret_cast<const unsigned char*>(tangents.data()),
            reinterpret_cast<const unsigned char*>(tangents.data()) + tangentSize);

        int tangentBufferView = CreateBufferView(model, 0, tangentOffset, tangentSize, TINYGLTF_TARGET_ARRAY_BUFFER);
        int tangentAccessor = CreateAccessor(model, tangentBufferView,
            TINYGLTF_COMPONENT_TYPE_FLOAT, static_cast<int>(vertices.size()), "VEC4");
        primitive.attributes["TANGENT"] = tangentAccessor;

        // === 写入索引数据 ===
        const auto& indices = assetModel.CPUIndices();
        if (!indices.empty())
        {
            size_t indicesOffset = buffer.data.size();
            size_t indicesSize = indices.size() * sizeof(uint32_t);
            buffer.data.insert(buffer.data.end(),
                reinterpret_cast<const unsigned char*>(indices.data()),
                reinterpret_cast<const unsigned char*>(indices.data()) + indicesSize);

            int indicesBufferView = CreateBufferView(model, 0, indicesOffset, indicesSize,
                TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
            int indicesAccessor = CreateAccessor(model, indicesBufferView,
                TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, static_cast<int>(indices.size()), "SCALAR");
            primitive.indices = indicesAccessor;
        }

        // === 关联材质 ===
        // 从RenderComponent获取材质信息
        if (modelToRenderComp.find(modelIdx) != modelToRenderComp.end())
        {
            auto renderComp = modelToRenderComp[modelIdx];
            const auto& materials = renderComp->GetMaterials();
            // 使用第一个材质索引（如果Mesh有多个Section，这是简化处理）
            primitive.material = static_cast<int>(materials[0]);
        }
        else
        {
            // 如果找不到RenderComponent，使用默认材质
            primitive.material = 0;
        }

        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);
    }

    model.buffers.push_back(buffer);
}

void FSceneSaver::SerializeMaterials(tinygltf::Model& model, const Scene& scene)
{
    const auto& materials = scene.Materials();

    for (const auto& mat : materials)
    {
        tinygltf::Material gltfMat;
        gltfMat.name = mat.name_;

        const auto& gpuMat = mat.gpuMaterial_;

        // 基础颜色（注意：加载时做了sqrt进行gamma转线性，保存时平方回去）
        gltfMat.pbrMetallicRoughness.baseColorFactor = {
            gpuMat.Diffuse.r * gpuMat.Diffuse.r,
            gpuMat.Diffuse.g * gpuMat.Diffuse.g,
            gpuMat.Diffuse.b * gpuMat.Diffuse.b,
            gpuMat.Diffuse.a
        };

        // 金属度和粗糙度
        gltfMat.pbrMetallicRoughness.metallicFactor = gpuMat.Metalness;
        gltfMat.pbrMetallicRoughness.roughnessFactor = gpuMat.Fuzziness;

        // TODO: 纹理将通过GPU回读方式添加

        // 发光强度
        if (gpuMat.MaterialModel == Material::Enum::DiffuseLight)
        {
            gltfMat.emissiveFactor = {
                gpuMat.Diffuse.r, gpuMat.Diffuse.g, gpuMat.Diffuse.b
            };
        }

        // 折射率扩展
        if (std::abs(gpuMat.RefractionIndex - 1.46f) > 0.01f)
        {
            tinygltf::Value iorExt(tinygltf::Value::Object{
                {"ior", tinygltf::Value(static_cast<double>(gpuMat.RefractionIndex))}
            });
            gltfMat.extensions["KHR_materials_ior"] = iorExt;
        }

        // 透射扩展
        if (gpuMat.MaterialModel == Material::Enum::Dielectric)
        {
            tinygltf::Value transExt(tinygltf::Value::Object{
                {"transmissionFactor", tinygltf::Value(1.0)}
            });
            gltfMat.extensions["KHR_materials_transmission"] = transExt;
        }

        model.materials.push_back(gltfMat);
    }
}

void FSceneSaver::SerializeCameras(tinygltf::Model& model, const Scene& scene)
{
    const auto& cameras = scene.GetCameras();

    for (const auto& cam : cameras)
    {
        tinygltf::Camera gltfCam;
        gltfCam.name = cam.name;
        gltfCam.type = "perspective";

        // 设置透视相机参数
        gltfCam.perspective.aspectRatio = 0;  // 0表示使用视口宽高比
        gltfCam.perspective.yfov = glm::radians(cam.FieldOfView);  // 转换为弧度
        gltfCam.perspective.znear = cam.NearPlane;
        gltfCam.perspective.zfar = cam.FarPlane;

        model.cameras.push_back(gltfCam);
    }
}

void FSceneSaver::CopyTextures(tinygltf::Model& outModel, const tinygltf::Model& sourceModel)
{
    // TODO: 实现通过GPU回读纹理数据的方式来保存纹理
    // 而不是从sourceModel复制，避免引发问题
    SPDLOG_WARN("Texture saving not implemented yet - will be added via GPU readback");
}

int FSceneSaver::AppendToBuffer(tinygltf::Buffer& buffer, const void* data, size_t size)
{
    int offset = static_cast<int>(buffer.data.size());
    const unsigned char* byteData = static_cast<const unsigned char*>(data);
    buffer.data.insert(buffer.data.end(), byteData, byteData + size);
    return offset;
}

int FSceneSaver::CreateBufferView(tinygltf::Model& model, int bufferIdx,
                                   size_t offset, size_t length, int target)
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

int FSceneSaver::CreateAccessor(tinygltf::Model& model, int bufferViewIdx,
                                 int componentType, int count, const std::string& type,
                                 const std::vector<double>& min, const std::vector<double>& max)
{
    tinygltf::Accessor accessor;
    accessor.bufferView = bufferViewIdx;
    accessor.componentType = componentType;
    accessor.count = count;

    // Convert type string to GLTF type constant
    if (type == "SCALAR")
    {
        accessor.type = TINYGLTF_TYPE_SCALAR;
    }
    else if (type == "VEC2")
    {
        accessor.type = TINYGLTF_TYPE_VEC2;
    }
    else if (type == "VEC3")
    {
        accessor.type = TINYGLTF_TYPE_VEC3;
    }
    else if (type == "VEC4")
    {
        accessor.type = TINYGLTF_TYPE_VEC4;
    }
    else
    {
        SPDLOG_ERROR("Unknown accessor type: {}", type);
        accessor.type = TINYGLTF_TYPE_SCALAR;
    }

    if (!min.empty())
    {
        accessor.minValues = min;
    }
    if (!max.empty())
    {
        accessor.maxValues = max;
    }
    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size() - 1);
}

}
