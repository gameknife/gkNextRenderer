#include "FSceneLoader.h"
#include "Common/CoreMinimal.hpp"
#include <cfloat>

#include "ThirdParty/mikktspace/mikktspace.h"
#include <spdlog/spdlog.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_ENABLE_DRACO

#define TINYGLTF_NO_STB_IMAGE
//#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "Material.hpp"
#include "Node.h"
#include "Utilities/FileHelper.hpp"

namespace Assets
{

    /* Functions to allow mikktspace library to interface with our mesh representation */
    static int MikktspaceGetNumFaces(const SMikkTSpaceContext *pContext)
    {
	    Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
	    return m->NumberOfIndices() / 3;
    }

    static int MikktspaceGetNumVerticesOfFace(
		    const SMikkTSpaceContext *pContext,
		    const int iFace)
    {
	    return 3;
    }

    static void MikktspaceGetPosition(const SMikkTSpaceContext *pContext, float fvPosOut[],
					    const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
	    auto v1 = m->CPUIndices()[iFace * 3 + iVert];

	    fvPosOut[0] = m->CPUVertices()[v1].Position.x;
	    fvPosOut[1] = m->CPUVertices()[v1].Position.y;
	    fvPosOut[2] = m->CPUVertices()[v1].Position.z;
    }

    static void MikktspaceGetNormal(const SMikkTSpaceContext *pContext, float fvNormOut[],
					    const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
        auto v1 = m->CPUIndices()[iFace * 3 + iVert];

	    fvNormOut[0] = m->CPUVertices()[v1].Normal.x;
	    fvNormOut[1] = m->CPUVertices()[v1].Normal.y;
	    fvNormOut[2] = m->CPUVertices()[v1].Normal.z;
    }

    static void MikktspaceGetTexCoord(const SMikkTSpaceContext *pContext, float fvTexcOut[],
					    const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
        auto v1 = m->CPUIndices()[iFace * 3 + iVert];

	    fvTexcOut[0] = m->CPUVertices()[v1].TexCoord.x;
	    fvTexcOut[1] = m->CPUVertices()[v1].TexCoord.y;
    }

    static void MikktspaceSetTSpaceBasic(const SMikkTSpaceContext *pContext, const float fvTangent[],
				    const float fSign, const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
        auto v1 = m->CPUIndices()[iFace * 3 + iVert];
        
        m->CPUVertices()[v1].Tangent = glm::vec4(fvTangent[0], fvTangent[1], fvTangent[2], fSign);
    }

    static SMikkTSpaceInterface MikktspaceInterface = {
        .m_getNumFaces			= MikktspaceGetNumFaces,
        .m_getNumVerticesOfFace		= MikktspaceGetNumVerticesOfFace,
        .m_getPosition			= MikktspaceGetPosition,
        .m_getNormal			= MikktspaceGetNormal,
        .m_getTexCoord			= MikktspaceGetTexCoord,
        .m_setTSpaceBasic		= MikktspaceSetTSpaceBasic,
    #if 0
        /* TODO: fill this in if needed */
        .m_setTSpace			= mikktspace_set_t_space,
    #else
        .m_setTSpace			= NULL,
    #endif
    };
    
    void ParseGltfNode(std::vector<std::shared_ptr<Assets::Node>>& outNodes, std::map<int, std::shared_ptr<Node> >& nodeMap, Assets::EnvironmentSetting& outCamera, std::vector<Assets::LightObject>& outLights,
        tinygltf::Model& model, int nodeIdx, int modelIdx, int materialOffset)
    {
        tinygltf::Node& node = model.nodes[nodeIdx];
        
        glm::vec3 translation;
        glm::vec3 scale;
        glm::quat rotation;

        if (!node.matrix.empty())
        {
            glm::dmat4 dmat = glm::make_mat4(node.matrix.data());
            glm::mat4 mat = glm::mat4(dmat);
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(mat, scale, rotation, translation, skew, perspective);
        }
        else
        {
            translation = node.translation.empty()
                               ? glm::vec3(0)
                               : glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            scale = node.scale.empty() ? glm::vec3(1) : glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
            rotation = node.rotation.empty()
                                       ? glm::quat(1, 0, 0, 0)
                                       : glm::quat(
                                           static_cast<float>(node.rotation[3]),
                                           static_cast<float>(node.rotation[0]),
                                           static_cast<float>(node.rotation[1]),
                                           static_cast<float>(node.rotation[2]));
        }

        uint32_t meshId = -1;
        if(node.mesh != -1)
        {
            meshId = node.mesh + modelIdx;
        }
        else
        {
            if(node.camera >= 0)
            {
                glm::mat4 t = glm::translate(glm::mat4(1), translation);
                glm::mat4 r = glm::toMat4(rotation);
                glm::mat4 s = glm::scale(glm::mat4(1), scale);
                
                glm::mat4 transform =  (t * r * s);
                
                vec4 camEye = transform * glm::vec4(0,0,0,1);
                vec4 camFwd = transform * glm::vec4(0,0,-1,0);
                glm::mat4 modelView = lookAt(vec3(camEye), vec3(camEye) + vec3(camFwd.x, camFwd.y, camFwd.z), glm::vec3(0, 1, 0));
                outCamera.cameras.push_back({ std::to_string(node.camera) + " " + node.name, modelView, 40});
            }
        }

        uint32_t primaryMatIdx = 0;
        std::shared_ptr<Node> sceneNode = Node::CreateNode(node.name, translation, rotation, scale, meshId, uint32_t(outNodes.size()), false);
        if (meshId != -1)
        {
            sceneNode->SetVisible(true);
            std::array<uint32_t, 16> materialIdx {};
            for (int i = 0; i < model.meshes[node.mesh].primitives.size(); i++)
            {
                assert( i < 16 );
                auto& primitive = model.meshes[node.mesh].primitives[i];
                materialIdx[i] = (max(0, primitive.material + materialOffset));
                primaryMatIdx = primitive.material + materialOffset;
            }
            sceneNode->SetMaterial(materialIdx);
        }

        if (node.skin != -1)
        {
            sceneNode->SetSkin(node.skin);
        }
        else
        {
            sceneNode->SetSkin(0xFFFFFFFF);
        }

        outNodes.push_back(sceneNode);

        nodeMap[nodeIdx] = sceneNode;

        if( node.extras.Has("arealight") )
        {
            // use the aabb to build a light, using the average normals and area
            // the basic of lightquad from blender is a 2 x 2 quad ,from -1 to 1
            glm::vec4 localP0 = glm::vec4(-1,0,-1, 1);
            glm::vec4 localP1 = glm::vec4(-1,0,1, 1);
            glm::vec4 localP3 = glm::vec4(1,0,-1, 1);

            auto transform = sceneNode->WorldTransform();
                
            LightObject light;
            light.p0 = transform * localP0;
            light.p1 = transform * localP1;
            light.p3 = transform * localP3;
            vec3 dir = vec3(transform * glm::vec4(0,1,0,0));
            light.normal_area = glm::vec4(glm::normalize(dir),0);
            light.normal_area.w = glm::length(glm::cross(glm::vec3(light.p1 - light.p0), glm::vec3(light.p3 - light.p0))) / 2.0f;
            light.lightMatIdx = primaryMatIdx;
            
            outLights.push_back(light);
        }
        
        // for each child node
        for (int child : node.children)
        {
            ParseGltfNode(outNodes, nodeMap, outCamera, outLights, model, child, modelIdx, materialOffset);
            nodeMap[child]->SetParent(sceneNode);
        }
    }
    
    bool LoadImageData(tinygltf::Image * image, const int imageIdx, std::string * err,
                   std::string * warn, int reqWidth, int reqHeight,
                   const unsigned char * bytes, int size, void * userData )
    {
        image->as_is = true;
        return true;
    }

    std::string FSceneLoader::currSceneName = "default";

    static glm::vec4 GetAttributeValue(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
    {
        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        const unsigned char* bufferData = model.buffers[view.buffer].data.data();
        const unsigned char* dataPtr = bufferData + view.byteOffset + accessor.byteOffset + index * accessor.ByteStride(view);

        auto readComponent = [&](const unsigned char* ptr) -> float {
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) return *reinterpret_cast<const float*>(ptr);
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) return static_cast<float>(*reinterpret_cast<const double*>(ptr));
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
                float v = static_cast<float>(*reinterpret_cast<const int8_t*>(ptr));
                return accessor.normalized ? std::max(v / 127.0f, -1.0f) : v;
            }
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                float v = static_cast<float>(*reinterpret_cast<const uint8_t*>(ptr));
                return accessor.normalized ? v / 255.0f : v;
            }
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
                float v = static_cast<float>(*reinterpret_cast<const int16_t*>(ptr));
                return accessor.normalized ? std::max(v / 32767.0f, -1.0f) : v;
            }
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                float v = static_cast<float>(*reinterpret_cast<const uint16_t*>(ptr));
                return accessor.normalized ? v / 65535.0f : v;
            }
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_INT) {
                 return static_cast<float>(*reinterpret_cast<const int*>(ptr));
            }
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) return static_cast<float>(*reinterpret_cast<const unsigned int*>(ptr));
            return 0.0f;
        };

        int componentSize = 1;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT || accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) componentSize = 2;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_INT || accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT || accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) componentSize = 4;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) componentSize = 8;

        int numComponents = 1;
        if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
        if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
        if (accessor.type == TINYGLTF_TYPE_VEC4) numComponents = 4;

        glm::vec4 res(0,0,0,1);

        for (int i = 0; i < numComponents && i < 4; ++i) {
            res[i] = readComponent(dataPtr + i * componentSize);
        }
        return res;
    }

    void FSceneLoader::GenerateMikkTSpace(Assets::Model *m)
    {
        SMikkTSpaceContext mikktspaceContext;

        mikktspaceContext.m_pInterface = &MikktspaceInterface;
        mikktspaceContext.m_pUserData = m;
        genTangSpaceDefault(&mikktspaceContext);
    }
    
    bool FSceneLoader::LoadGLTFScene(const std::string& filename, Assets::EnvironmentSetting& cameraInit, std::vector< std::shared_ptr<Assets::Node> >& nodes,
                              std::vector<Assets::Model>& models,
                              std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons)
    {
        int32_t materialOffset = static_cast<int32_t>(materials.size());
        int32_t modelIdx = static_cast<int32_t>(models.size());
        
        tinygltf::Model model;
        tinygltf::TinyGLTF gltfLoader;
        std::string err;
        std::string warn;
        std::filesystem::path filepath = filename;
        
        
        // load all textures
        std::vector<int32_t> textureIdMap;
        currSceneName = filepath.filename().string();

        gltfLoader.SetImagesAsIs(true);
        gltfLoader.SetImageLoader(LoadImageData, nullptr);
        if (filepath.extension() == ".glb")
        {
            // try fetch from pakcagesystem
            std::vector<uint8_t> data;
            if ( !Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data) )
            {
                SPDLOG_ERROR("failed to load file: {}", filename);
                return false;
            }
            if(!gltfLoader.LoadBinaryFromMemory(&model, &err, &warn, data.data(), uint32_t(data.size())) )
            {
                SPDLOG_ERROR("failed to parse glb file: {}", filename);
                return false;
            }
        }
        else
        {
            std::filesystem::path gltfFile(filename);
            if (gltfFile.is_relative())
            {
                gltfFile = ".." / gltfFile;
            }
            if(!gltfLoader.LoadASCIIFromFile(&model, &err, &warn, gltfFile.string()) )
            {
                SPDLOG_ERROR("failed to parse gltf file: {}\nErr: {}\nWarn: {}", filename, err, warn);
                return false;
            }
        }

        // delayed texture creation
        textureIdMap.resize(model.images.size(), -1);
        auto lambdaLoadTexture = [&textureIdMap, &model, filepath](int texture, bool srgb)
        {
            if (texture != -1 && texture < model.textures.size())
            {
                int imageIdx = model.textures[texture].source;
                if (model.textures[texture].extensions.find("EXT_texture_webp") != model.textures[texture].extensions.end())
                {
                     if (model.textures[texture].extensions["EXT_texture_webp"].Has("source"))
                     {
                         imageIdx = model.textures[texture].extensions["EXT_texture_webp"].Get("source").GetNumberAsInt();
                     }
                }

                if (imageIdx == -1) imageIdx = texture;

                if (imageIdx >= textureIdMap.size())
                {
                    return;
                }

                if (textureIdMap[imageIdx] != -1)
                {
                    return;
                }
                
                // create texture
                auto& image = model.images[imageIdx];

                // if (image.width <= 0 || image.height <= 0)
                // {
                //     return;
                // }

                std::string texname = image.name.empty() ? fmt::format("tex_{}", imageIdx) : image.name;
                if (image.bufferView == -1)
                {
                    // load from file
                    auto fileuri = filepath.parent_path() / image.uri;
                    uint32_t texIdx = GlobalTexturePool::LoadTexture(fileuri.string(), srgb);
                    textureIdMap[imageIdx] = texIdx;
                }
                else
                {
                    uint32_t texIdx = GlobalTexturePool::LoadTexture(
                        currSceneName + texname, model.images[imageIdx].mimeType,
                        model.buffers[0].data.data() + model.bufferViews[image.bufferView].byteOffset,
                        model.bufferViews[image.bufferView].byteLength, srgb);
                    textureIdMap[imageIdx] = texIdx;
                }
            }
        };

        auto lambdaGetTexture = [&textureIdMap, &model](int texture)
        {
            if (texture != -1)
            {
                int imageIdx = model.textures[texture].source;
                if (model.textures[texture].extensions.find("EXT_texture_webp") != model.textures[texture].extensions.end())
                {
                     if (model.textures[texture].extensions["EXT_texture_webp"].Has("source"))
                     {
                         imageIdx = model.textures[texture].extensions["EXT_texture_webp"].Get("source").GetNumberAsInt();
                     }
                }
                
                if (imageIdx == -1) imageIdx = texture;

                if (imageIdx >= textureIdMap.size())
                {
                    return -1;
                }

                return textureIdMap[imageIdx];
            }
            return -1;
        };
        
        for (tinygltf::Material& mat : model.materials)
        {
            lambdaLoadTexture(mat.pbrMetallicRoughness.baseColorTexture.index, true);
            lambdaLoadTexture(mat.pbrMetallicRoughness.metallicRoughnessTexture.index, false);
            lambdaLoadTexture(mat.normalTexture.index, false);
            lambdaLoadTexture(mat.occlusionTexture.index, false);
            lambdaLoadTexture(mat.emissiveTexture.index, true);
        }
        
        
                        // Load skeletons
        
        
                        for (const auto& skin : model.skins)
        
        
                        {
        
        
                             Assets::Skeleton skeleton;
        
        
                             skeleton.Name = skin.name;
        
        
                             skeleton.RootJointIndex = -1; // TODO: find root? usually 0 or logic derived
        
        
                             
        
        
                             std::vector<glm::mat4> ibms;
        
        
                             if (skin.inverseBindMatrices != -1)
        
        
                             {
        
        
                                 const auto& accessor = model.accessors[skin.inverseBindMatrices];
        
        
                                 const auto& bufferView = model.bufferViews[accessor.bufferView];
        
        
                                 const auto& buffer = model.buffers[bufferView.buffer];
        
        
                                 int stride = accessor.ByteStride(bufferView);
        
        
                                 
        
        
                                 ibms.resize(accessor.count);
        
        
                                 const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
        
        
                                 
        
        
                                 for(size_t i=0; i<accessor.count; ++i)
        
        
                                 {
        
        
                                     const float* m = reinterpret_cast<const float*>(dataPtr + i * stride);
        
        
                                     ibms[i] = glm::make_mat4(m);
        
        
                                 }
        
        
                             }
        
        
                
        
        
                             std::map<int, int> nodeToJointIndex;
        
        
                             for(size_t i=0; i<skin.joints.size(); ++i)
        
        
                             {
        
        
                                 nodeToJointIndex[skin.joints[i]] = static_cast<int>(i);
        
        
                             }
        
        
                
        
        
                             for (size_t i = 0; i < skin.joints.size(); ++i)
        
        
                             {
        
        
                                 int nodeIdx = skin.joints[i];
        
        
                                 auto& node = model.nodes[nodeIdx];
        
        
                                 
        
        
                                 Assets::Joint joint;
        
        
                                 joint.Name = node.name;
        
        
                                 if (i < ibms.size())
        
        
                                 {
        
        
                                     joint.InverseBindMatrix = ibms[i];
        
        
                                 }
        
        
                                 
        
        
                                 if (!node.matrix.empty())
        
        
                                 {
        
        
                                     glm::dmat4 dmat = glm::make_mat4(node.matrix.data());
        
        
                                     glm::mat4 mat = glm::mat4(dmat);
        
        
                                     glm::vec3 skew;
        
        
                                     glm::vec4 perspective;
        
        
                                     glm::decompose(mat, joint.Scale, joint.Rotation, joint.Translation, skew, perspective);
        
        
                                 }
        
        
                                 else
        
        
                                 {
        
        
                                    joint.Translation = node.translation.empty() ? glm::vec3(0) : glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        
        
                                    joint.Scale = node.scale.empty() ? glm::vec3(1) : glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        
        
                                    joint.Rotation = node.rotation.empty() ? glm::quat(1, 0, 0, 0) : glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        
        
                                 }
        
        
                                 
        
        
                                 skeleton.Joints.push_back(joint);
        
        
                             }
        
        
                
        
        
                             // Hierarchy
        
        
                             for(size_t i=0; i<skin.joints.size(); ++i)
        
        
                             {
        
        
                                 int nodeIdx = skin.joints[i];
        
        
                                 auto& node = model.nodes[nodeIdx];
        
        
                                 for (int child : node.children)
        
        
                                 {
        
        
                                     if (nodeToJointIndex.find(child) != nodeToJointIndex.end())
        
        
                                     {
        
        
                                         int childIdx = nodeToJointIndex[child];
        
        
                                         skeleton.Joints[childIdx].ParentIndex = static_cast<int>(i);
        
        
                                         skeleton.Joints[i].Children.push_back(childIdx);
        
        
                                     }
        
        
                                 }
        
        
                             }
        
        
                             
        
        
                             skeletons.push_back(skeleton);
        
        
                        }
        
        
                
                // load all materials
        for (tinygltf::Material& mat : model.materials)
        {
            Material m{};

            m.DiffuseTextureId = -1;
            m.MRATextureId = -1;
            m.NormalTextureId = -1;
            m.EmissiveTextureId = -1;
            m.NormalTextureScale = 1.0f;

            m.MaterialModel = Material::Enum::Mixture;
            m.Fuzziness = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);
            m.Metalness = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor);
            m.RefractionIndex = 1.46f;
            m.RefractionIndex2 = 1.46f;
            
            m.DiffuseTextureId = lambdaGetTexture( mat.pbrMetallicRoughness.baseColorTexture.index );
            m.MRATextureId = lambdaGetTexture(mat.pbrMetallicRoughness.metallicRoughnessTexture.index); // metallic in B, roughness in G
            
            m.NormalTextureId = lambdaGetTexture(mat.normalTexture.index);
            m.NormalTextureScale = static_cast<float>(mat.normalTexture.scale);

            if (mat.occlusionTexture.index != -1 && mat.occlusionTexture.index != mat.pbrMetallicRoughness.metallicRoughnessTexture.index)
            {
                SPDLOG_WARN("Material '{}': Separate Occlusion texture not supported. Pack it into the R channel of Metallic-Roughness texture.", mat.name);
            }
            if (mat.alphaMode != "OPAQUE")
            {
                 SPDLOG_WARN("Material '{}': Alpha mode '{}' not fully supported (assumed OPAQUE).", mat.name, mat.alphaMode);
            }
            if (mat.doubleSided)
            {
                 SPDLOG_WARN("Material '{}': Double sided not supported.", mat.name);
            }
            
            glm::vec3 emissiveColor = mat.emissiveFactor.empty()
                                          ? glm::vec3(0)
                                          : glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1],
                                                      mat.emissiveFactor[2]);
            glm::vec3 diffuseColor = mat.pbrMetallicRoughness.baseColorFactor.empty()
                                         ? glm::vec3(1)
                                         : glm::vec3(mat.pbrMetallicRoughness.baseColorFactor[0],
                                                     mat.pbrMetallicRoughness.baseColorFactor[1],
                                                     mat.pbrMetallicRoughness.baseColorFactor[2]);

            m.Diffuse = glm::vec4(sqrt(diffuseColor), 1.0);

            if (m.MRATextureId != -1)
            {
                // m.Metalness = 1.0f; that makes huge problem, first dont change this
                m.Fuzziness = 1.0f;
            }

            if (m.Metalness > .95 && m.MRATextureId == -1)
            {
                m.MaterialModel = Material::Enum::Metallic;
            }

            if (m.Fuzziness > .95 && m.MRATextureId == -1 && m.Metalness < 0.01)
            {
                m.MaterialModel = Material::Enum::Lambertian;
            }

            auto ior = mat.extensions.find("KHR_materials_ior");
            if( ior != mat.extensions.end())
            {
                m.RefractionIndex = static_cast<float>(ior->second.Get("ior").GetNumberAsDouble());
                m.RefractionIndex2 = m.RefractionIndex;
            }

            auto transmission = mat.extensions.find("KHR_materials_transmission");
            if( transmission != mat.extensions.end())
            {
                float trans = static_cast<float>(transmission->second.Get("transmissionFactor").GetNumberAsDouble());
                if(trans > 0.8)
                {
                   m.MaterialModel = Material::Enum::Dielectric;
                }
            }

            if( mat.extras.Has("ior2") )
            {
                m.RefractionIndex2 = mat.extras.Get("ior2").GetNumberAsDouble();
            }

            auto emissive = mat.extensions.find("KHR_materials_emissive_strength");
            // TODO: may impl per pixel mat type
            if (emissive != mat.extensions.end())
            {
                float power = static_cast<float>(emissive->second.Get("emissiveStrength").GetNumberAsDouble());
                if (mat.emissiveTexture.index != -1)
                {
                    m.EmissiveTextureId = lambdaGetTexture(mat.emissiveTexture.index);
                }
            }
            else if (glm::length(emissiveColor) > 0.0f)
            {
                if (mat.emissiveTexture.index != -1)
                {
                    m.EmissiveTextureId = lambdaGetTexture(mat.emissiveTexture.index);
                }
            }

            materials.push_back( { m, mat.name } );
        }

        // export whole scene into a big buffer, with vertice indices materials
        for (tinygltf::Mesh& mesh : model.meshes)
        {
            bool hasTangent = false;
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<glm::vec4> weights;
            std::vector<glm::uvec4> joints;

            uint32_t vertextOffset = 0;
            uint32_t sectionIdx = 0;
            for (tinygltf::Primitive& primtive : mesh.primitives)
            {
                if (primtive.mode != TINYGLTF_MODE_TRIANGLES)
                {
                    continue;
                }

                if (primtive.indices != -1)
                {
                    const tinygltf::Accessor& indexAccessor = model.accessors[primtive.indices];
                    if (indexAccessor.count == 0)
                    {
                        continue;
                    }
                }
               
                int posIdx = -1;
                if (primtive.attributes.find("POSITION") != primtive.attributes.end()) posIdx = primtive.attributes["POSITION"];
                int normIdx = -1;
                if (primtive.attributes.find("NORMAL") != primtive.attributes.end()) normIdx = primtive.attributes["NORMAL"];
                int uvIdx = -1;
                if (primtive.attributes.find("TEXCOORD_0") != primtive.attributes.end()) uvIdx = primtive.attributes["TEXCOORD_0"];
                int tanIdx = -1;
                if (primtive.attributes.find("TANGENT") != primtive.attributes.end()) tanIdx = primtive.attributes["TANGENT"];

                if (primtive.attributes.find("TEXCOORD_1") != primtive.attributes.end())
                {
                     // SPDLOG_WARN("Mesh '{}': TEXCOORD_1 found but ignored.", mesh.name);
                }
                
                int jointsIdx = -1;
                if (primtive.attributes.find("JOINTS_0") != primtive.attributes.end()) jointsIdx = primtive.attributes["JOINTS_0"];
                int weightsIdx = -1;
                if (primtive.attributes.find("WEIGHTS_0") != primtive.attributes.end()) weightsIdx = primtive.attributes["WEIGHTS_0"];

                if (posIdx == -1) continue;

                tinygltf::Accessor positionAccessor = model.accessors[posIdx];
                if (tanIdx != -1) hasTangent = true;
                
                for (size_t i = 0; i < positionAccessor.count; ++i)
                {
                    Vertex vertex;
                    vertex.Position = glm::vec3(GetAttributeValue(model, positionAccessor, i));

                    if (normIdx != -1)
                        vertex.Normal = glm::vec3(GetAttributeValue(model, model.accessors[normIdx], i));
                    else
                        vertex.Normal = glm::vec3(0, 1, 0);

                    if (tanIdx != -1)
                        vertex.Tangent = GetAttributeValue(model, model.accessors[tanIdx], i);

                    if (uvIdx != -1)
                        vertex.TexCoord = glm::vec2(GetAttributeValue(model, model.accessors[uvIdx], i));
                    
                    vertex.MaterialIndex = sectionIdx;
                    vertices.push_back(vertex);

                    if (jointsIdx != -1) {
                         joints.push_back(glm::uvec4(GetAttributeValue(model, model.accessors[jointsIdx], i)));
                    } else {
                         joints.push_back(glm::uvec4(0));
                    }
                    
                    if (weightsIdx != -1) {
                         weights.push_back(GetAttributeValue(model, model.accessors[weightsIdx], i));
                    } else {
                         weights.push_back(glm::vec4(0));
                    }
                }
                
                sectionIdx++;
                
                if (primtive.indices != -1)
                {
                    const tinygltf::Accessor& indexAccessor = model.accessors[primtive.indices];
                    const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
                    int strideIndex = indexAccessor.ByteStride(indexView);

                    for (size_t i = 0; i < indexAccessor.count; ++i)
                    {
                        if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT )
                        {
                            uint16* data = reinterpret_cast<uint16*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                            indices.push_back(*data + vertextOffset);
                        }
                        else if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT )
                        {
                            uint32* data = reinterpret_cast<uint32*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                            indices.push_back(*data + vertextOffset);
                        }
                        else if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE )
                        {
                            uint8_t* data = reinterpret_cast<uint8_t*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                            indices.push_back(*data + vertextOffset);
                        }
                        else
                        {
                            SPDLOG_ERROR("Unsupported index component type: {}", indexAccessor.componentType);
                            assert(0);
                        }
                    }
                }
                else
                {
                    for (size_t i = 0; i < positionAccessor.count; ++i)
                    {
                        indices.push_back(static_cast<uint32_t>(i) + vertextOffset);
                    }
                }

                vertextOffset += static_cast<uint32_t>(positionAccessor.count);
            }
            
            models.push_back(Assets::Model(mesh.name, std::move(vertices), std::move(indices), !hasTangent));
            if (!weights.empty())
            {
                models.back().CPUWeights() = std::move(weights);
                models.back().CPUJoints() = std::move(joints);
            }
        }



        auto& root = model.scenes[0];
        if(root.extras.Has("SkyIdx"))
        {
            cameraInit.HasSky = true;
            cameraInit.SkyIdx = root.extras.Get("SkyIdx").GetNumberAsInt();
        }
        if(root.extras.Has("SkyIntensity"))
        {
            cameraInit.HasSky = true;
            cameraInit.SkyIntensity = root.extras.Get("SkyIntensity").GetNumberAsDouble();
        }
        if(root.extras.Has("SkyRotation"))
        {
            cameraInit.HasSky = true;
            cameraInit.SkyRotation = root.extras.Get("SkyRotation").GetNumberAsDouble();
        }
        if(root.extras.Has("SunIntensity"))
        {
            cameraInit.HasSun = true;
            cameraInit.SunIntensity = root.extras.Get("SunIntensity").GetNumberAsDouble();
        }
        if(root.extras.Has("CamSpeed"))
        {
            cameraInit.ControlSpeed = static_cast<float>(root.extras.Get("CamSpeed").GetNumberAsDouble());
        }
        if(root.extras.Has("WithSun"))
        {
            cameraInit.HasSun = root.extras.Get("WithSun").GetNumberAsInt() != 0;
        }
        if(root.extras.Has("SunRotation"))
        {
            cameraInit.SunRotation = static_cast<float>(root.extras.Get("SunRotation").GetNumberAsDouble());
        }
        if(root.extras.Has("NoSky"))
        {
            cameraInit.HasSky = false;
        }

        // gltf scenes contain the rootnodes
        //std::shared_ptr<Node> sceneNode = Node::CreateNode("Root", glm::vec3(0,0,0), glm::quat(1,0,0,0), glm::vec3(10,10,10), -1, nodes.size(), false);
        //nodes.push_back(sceneNode);
        
        std::map<int, std::shared_ptr<Node> > nodeMap;
        for (int nodeIdx : model.scenes[0].nodes)
        {
            ParseGltfNode(nodes, nodeMap, cameraInit, lights, model, nodeIdx, modelIdx, materialOffset);
            // if (nodeMap.find(nodeIdx) != nodeMap.end())
            // {
            //     nodeMap[nodeIdx]->SetParent(sceneNode);
            // }
        }

        // default auto camera
        Camera defaultCam = FSceneLoader::AutoFocusCamera(cameraInit, nodes, models);

        // if no camera, add default
        if (cameraInit.cameras.empty() )
        {
            cameraInit.cameras.push_back(defaultCam);
        }

        // load all animations
        for ( auto& animation : model.animations )
        {
            std::map<std::string, AnimationTrack> trackMaps;
            for ( auto& track : animation.channels )
            {
                if (animation.samplers[track.sampler].interpolation == "CUBICSPLINE")
                {
                    SPDLOG_WARN("Animation '{}': Cubic Spline interpolation not supported. Track ignored.", animation.name);
                    continue;
                }

                if (track.target_path == "scale")
                {
                    tinygltf::Accessor inputAccessor = model.accessors[animation.samplers[track.sampler].input];
                    tinygltf::Accessor outputAccessor = model.accessors[animation.samplers[track.sampler].output];

                    tinygltf::BufferView inputView = model.bufferViews[inputAccessor.bufferView];
                    tinygltf::BufferView outputView = model.bufferViews[outputAccessor.bufferView];

                    std::string nodeName = model.nodes[track.target_node].name;
                    AnimationTrack& createTrack = trackMaps[nodeName];

                    createTrack.AnimationName = animation.name == "" ? "Default" : animation.name;
                    createTrack.NodeName_ = nodeName;
                    createTrack.Time_ = 0;

                    int inputStride = inputAccessor.ByteStride(inputView);
                    int outputStride = outputAccessor.ByteStride(outputView);

                    for (size_t i = 0; i < inputAccessor.count; ++i)
                    {
                        float time = 0.f;
                        glm::vec3 translation;
                        if ( inputAccessor.type == TINYGLTF_TYPE_SCALAR )
                        {

                            float* position = reinterpret_cast<float*>(&model.buffers[inputView.buffer].data[inputView.byteOffset + inputAccessor.byteOffset + i *
                                inputStride]);
                            time = position[0];
                        }

                        if ( outputAccessor.type == TINYGLTF_TYPE_VEC3 )
                        {
                            float* position = reinterpret_cast<float*>(&model.buffers[outputView.buffer].data[outputView.byteOffset + outputAccessor.byteOffset + i *
                                outputStride]);
                            translation = vec3(
                                position[0],
                                position[1],
                                position[2]
                            );
                        }

                        AnimationKey<glm::vec3> key;
                        key.Time = time;
                        key.Value = translation;

                        createTrack.ScaleChannel.Keys.push_back(key);
                        createTrack.Duration_ = max(time, createTrack.Duration_);
                    }
                }
                if (track.target_path == "rotation")
                {
                    tinygltf::Accessor inputAccessor = model.accessors[animation.samplers[track.sampler].input];
                    tinygltf::Accessor outputAccessor = model.accessors[animation.samplers[track.sampler].output];

                    tinygltf::BufferView inputView = model.bufferViews[inputAccessor.bufferView];
                    tinygltf::BufferView outputView = model.bufferViews[outputAccessor.bufferView];
                    
                    std::string nodeName = model.nodes[track.target_node].name;
                    AnimationTrack& createTrack = trackMaps[nodeName];

                    createTrack.AnimationName = animation.name == "" ? "Default" : animation.name;
                    createTrack.NodeName_ = nodeName;
                    createTrack.Time_ = 0;

                    int inputStride = inputAccessor.ByteStride(inputView);
                    int outputStride = outputAccessor.ByteStride(outputView);

                    for (size_t i = 0; i < inputAccessor.count; ++i)
                    {
                        float time = 0.f;
                        glm::quat rotation;
                        if ( inputAccessor.type == TINYGLTF_TYPE_SCALAR )
                        {
                            
                            float* position = reinterpret_cast<float*>(&model.buffers[inputView.buffer].data[inputView.byteOffset + inputAccessor.byteOffset + i *
                                inputStride]);
                            time = position[0];
                        }

                        if ( outputAccessor.type == TINYGLTF_TYPE_VEC4 )
                        {
                            float* position = reinterpret_cast<float*>(&model.buffers[outputView.buffer].data[outputView.byteOffset + outputAccessor.byteOffset + i *
                                outputStride]);
                            rotation = glm::quat(
                                position[3],
                                position[0],
                                position[1],
                                position[2]
                            );
                        }

                        AnimationKey<glm::quat> key;
                        key.Time = time;
                        key.Value = rotation;
                        
                        createTrack.RotationChannel.Keys.push_back(key);
                        createTrack.Duration_ = max(time, createTrack.Duration_);
                    }
                }
                if (track.target_path == "translation")
                {
                    tinygltf::Accessor inputAccessor = model.accessors[animation.samplers[track.sampler].input];
                    tinygltf::Accessor outputAccessor = model.accessors[animation.samplers[track.sampler].output];

                    tinygltf::BufferView inputView = model.bufferViews[inputAccessor.bufferView];
                    tinygltf::BufferView outputView = model.bufferViews[outputAccessor.bufferView];
                    
                    std::string nodeName = model.nodes[track.target_node].name;
                    AnimationTrack& createTrack = trackMaps[nodeName];
                    
                    createTrack.AnimationName = animation.name == "" ? "Default" : animation.name;
                    createTrack.NodeName_ = nodeName;
                    createTrack.Time_ = 0;

                    int inputStride = inputAccessor.ByteStride(inputView);
                    int outputStride = outputAccessor.ByteStride(outputView);

                    for (size_t i = 0; i < inputAccessor.count; ++i)
                    {
                        float time = 0.f;
                        glm::vec3 translation;
                        if ( inputAccessor.type == TINYGLTF_TYPE_SCALAR )
                        {
                            
                            float* position = reinterpret_cast<float*>(&model.buffers[inputView.buffer].data[inputView.byteOffset + inputAccessor.byteOffset + i *
                                inputStride]);
                            time = position[0];
                        }

                        if ( outputAccessor.type == TINYGLTF_TYPE_VEC3 )
                        {
                            float* position = reinterpret_cast<float*>(&model.buffers[outputView.buffer].data[outputView.byteOffset + outputAccessor.byteOffset + i *
                                outputStride]);
                            translation = vec3(
                                position[0],
                                position[1],
                                position[2]
                            );
                        }

                        AnimationKey<glm::vec3> key;
                        key.Time = time;
                        key.Value = translation;

                        createTrack.TranslationChannel.Keys.push_back(key);
                        createTrack.Duration_ = max(time, createTrack.Duration_);
                    }
                }
            }

            for ( auto& track : trackMaps )
            {
                tracks.push_back(track.second);
            }
        }

        // if we got camera in the scene
        int i = 0;
        for (tinygltf::Camera& cam : model.cameras)
        {
            cameraInit.cameras[i].Aperture = 0.0f;
            cameraInit.cameras[i].FocalDistance = 1000.0f;
            cameraInit.cameras[i].FieldOfView = static_cast<float>(cam.perspective.yfov) * 180.f / float(M_PI);
            cameraInit.cameras[i].NearPlane = static_cast<float>(cam.perspective.znear);
            cameraInit.cameras[i].FarPlane = static_cast<float>(cam.perspective.zfar);
            if( cam.extras.Has("F-Stop") )
            {
                cameraInit.cameras[i].Aperture = 0.2f / cam.extras.Get("F-Stop").GetNumberAsDouble();
            }
            if( cam.extras.Has("FocalDistance") )
            {
                cameraInit.cameras[i].FocalDistance = cam.extras.Get("FocalDistance").GetNumberAsDouble();
            }
            i++;
        }
        //printf("model.cameras: %d\n", i);
        return true;
    }

    Camera FSceneLoader::AutoFocusCamera(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Model>& models)
    {
        //auto center camera by scene bounds
        glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
        bool hasModel = false;

        for (const auto& node : nodes)
        {
            if (node->IsDrawable())
            {
                uint32_t modelIdx = node->GetModel();
                if (modelIdx < models.size())
                {
                    auto& model = models[modelIdx];
                    glm::vec3 aabbMin = model.GetLocalAABBMin();
                    glm::vec3 aabbMax = model.GetLocalAABBMax();

                    // Transform 8 corners
                    glm::vec3 corners[8] = {
                        {aabbMin.x, aabbMin.y, aabbMin.z},
                        {aabbMax.x, aabbMin.y, aabbMin.z},
                        {aabbMin.x, aabbMax.y, aabbMin.z},
                        {aabbMax.x, aabbMax.y, aabbMin.z},
                        {aabbMin.x, aabbMin.y, aabbMax.z},
                        {aabbMax.x, aabbMin.y, aabbMax.z},
                        {aabbMin.x, aabbMax.y, aabbMax.z},
                        {aabbMax.x, aabbMax.y, aabbMax.z}
                    };

                    const glm::mat4& worldTransform = node->WorldTransform();

                    for (int k = 0; k < 8; k++)
                    {
                        glm::vec4 worldPos = worldTransform * glm::vec4(corners[k], 1.0f);
                        boundsMin = glm::min(boundsMin, glm::vec3(worldPos));
                        boundsMax = glm::max(boundsMax, glm::vec3(worldPos));
                    }
                    hasModel = true;
                }
            }
        }

        if (!hasModel)
        {
             boundsMin = glm::vec3(-10, -10, -10);
             boundsMax = glm::vec3(10, 10, 10);
        }

        glm::vec3 boundsCenter = (boundsMax - boundsMin) * 0.5f + boundsMin;
        float radius = length(boundsMax - boundsMin);

        Camera newCamera;
        newCamera.ModelView = lookAt(vec3(boundsCenter.x, boundsCenter.y, boundsCenter.z + radius * 1.5f), boundsCenter, vec3(0, 1, 0));
        newCamera.FieldOfView = 40;
        newCamera.Aperture = 0.0f;
        newCamera.name = "AutoCamera";
        newCamera.NearPlane = glm::min( radius * 0.5f, 0.2f);
        newCamera.FarPlane = glm::min( radius * 100.f, 1000.f);

        return newCamera;
    }
}
