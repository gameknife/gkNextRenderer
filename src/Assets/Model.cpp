#include "Model.hpp"
#include "CornellBox.hpp"
#include "Procedural.hpp"
#include "Sphere.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Math.hpp"
#include "ThirdParty/mikktspace/mikktspace.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <tiny_obj_loader.h>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <unordered_map>
#include <vector>

#define _USE_MATH_DEFINES
#include <math.h>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_ENABLE_DRACO

#if !ANDROID
#define TINYGLTF_USE_RAPIDJSON
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#define TINYGLTF_NO_INCLUDE_RAPIDJSON
#endif

#define TINYGLTF_NO_STB_IMAGE
//#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include <fmt/format.h>
#include <glm/gtx/matrix_decompose.hpp>

#include "Options.hpp"
#include "Texture.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/NextPhysics.h"

#define FLATTEN_VERTICE 1

using namespace glm;

namespace std
{
    template <>
    struct hash<Assets::Vertex> final
    {
        size_t operator()(Assets::Vertex const& vertex) const noexcept
        {
            return
                Combine(hash<vec3>()(vertex.Position),
                        Combine(hash<vec3>()(vertex.Normal),
                                Combine(hash<vec2>()(vertex.TexCoord),
                                        hash<int>()(vertex.MaterialIndex))));
        }

    private:
        static size_t Combine(size_t hash0, size_t hash1)
        {
            return hash0 ^ (hash1 + 0x9e3779b9 + (hash0 << 6) + (hash0 >> 2));
        }
    };
}

namespace Assets
{
    
    /* Functions to allow mikktspace library to interface with our mesh representation */
    static int mikktspace_get_num_faces(const SMikkTSpaceContext *pContext)
    {
	    Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
	    return m->NumberOfIndices() / 3;
    }

    static int mikktspace_get_num_vertices_of_face(
		    const SMikkTSpaceContext *pContext,
		    const int iFace)
    {
	    return 3;
    }

    static void mikktspace_get_position(const SMikkTSpaceContext *pContext, float fvPosOut[],
					    const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
	    auto v1 = m->CPUIndices()[iFace * 3 + iVert];

	    fvPosOut[0] = m->CPUVertices()[v1].Position.x;
	    fvPosOut[1] = m->CPUVertices()[v1].Position.y;
	    fvPosOut[2] = m->CPUVertices()[v1].Position.z;
    }

    static void mikktspace_get_normal(const SMikkTSpaceContext *pContext, float fvNormOut[],
					    const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
        auto v1 = m->CPUIndices()[iFace * 3 + iVert];

	    fvNormOut[0] = m->CPUVertices()[v1].Normal.x;
	    fvNormOut[1] = m->CPUVertices()[v1].Normal.y;
	    fvNormOut[2] = m->CPUVertices()[v1].Normal.z;
    }

    static void mikktspace_get_tex_coord(const SMikkTSpaceContext *pContext, float fvTexcOut[],
					    const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
        auto v1 = m->CPUIndices()[iFace * 3 + iVert];

	    fvTexcOut[0] = m->CPUVertices()[v1].TexCoord.x;
	    fvTexcOut[1] = m->CPUVertices()[v1].TexCoord.y;
    }

    static void mikktspace_set_t_space_basic(const SMikkTSpaceContext *pContext, const float fvTangent[],
				    const float fSign, const int iFace, const int iVert)
    {
        Assets::Model *m = reinterpret_cast<Assets::Model*>(pContext->m_pUserData);
        auto v1 = m->CPUIndices()[iFace * 3 + iVert];
        
        m->CPUVertices()[v1].Tangent = glm::vec4(fvTangent[0], fvTangent[1], fvTangent[2], fSign);
    }

    static SMikkTSpaceInterface mikktspace_interface = {
        .m_getNumFaces			= mikktspace_get_num_faces,
        .m_getNumVerticesOfFace		= mikktspace_get_num_vertices_of_face,
        .m_getPosition			= mikktspace_get_position,
        .m_getNormal			= mikktspace_get_normal,
        .m_getTexCoord			= mikktspace_get_tex_coord,
        .m_setTSpaceBasic		= mikktspace_set_t_space_basic,
    #if 0
        /* TODO: fill this in if needed */
        .m_setTSpace			= mikktspace_set_t_space,
    #else
        .m_setTSpace			= NULL,
    #endif
    };
    
    void GenerateMikkTSpace(Assets::Model *m)
    {
        SMikkTSpaceContext mikktspace_context;

        mikktspace_context.m_pInterface = &mikktspace_interface;
        mikktspace_context.m_pUserData = m;
        genTangSpaceDefault(&mikktspace_context);
    }
    
    void ParseGltfNode(std::vector<std::shared_ptr<Assets::Node>>& out_nodes, std::map<int, std::shared_ptr<Node> >& nodeMap, Assets::EnvironmentSetting& out_camera, std::vector<Assets::LightObject>& out_lights,
        tinygltf::Model& model, int node_idx, int modelIdx)
    {
        tinygltf::Node& node = model.nodes[node_idx];
        
        glm::vec3 translation = node.translation.empty()
                           ? glm::vec3(0)
                           : glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        glm::vec3 scale = node.scale.empty() ? glm::vec3(1) : glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        glm::quat rotation = node.rotation.empty()
                                   ? glm::quat(1, 0, 0, 0)
                                   : glm::quat(
                                       static_cast<float>(node.rotation[3]),
                                       static_cast<float>(node.rotation[0]),
                                       static_cast<float>(node.rotation[1]),
                                       static_cast<float>(node.rotation[2]));
        rotation = glm::normalize(rotation);

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
                glm::mat4 ModelView = lookAt(vec3(camEye), vec3(camEye) + vec3(camFwd.x, camFwd.y, camFwd.z), glm::vec3(0, 1, 0));
                out_camera.cameras.push_back({ std::to_string(node.camera) + " " + node.name, ModelView, 40});
            }
        }
        
        std::shared_ptr<Node> sceneNode = Node::CreateNode(node.name, translation, rotation, scale, meshId, out_nodes.size(), false);
        if (meshId != -1)
        {
            sceneNode->SetVisible(true);
            std::vector<uint32_t> materialIdx;
            for (int i = 0; i < model.meshes[node.mesh].primitives.size(); i++)
            {
                auto& primitive = model.meshes[node.mesh].primitives[i];
                materialIdx.push_back(max(0, primitive.material));
            }
            sceneNode->SetMaterial(materialIdx);
        }
        out_nodes.push_back(sceneNode);

        nodeMap[node_idx] = sceneNode;

        if( node.extras.Has("arealight") )
        {
            // use the aabb to build a light, using the average normals and area
            // the basic of lightquad from blender is a 2 x 2 quad ,from -1 to 1
            glm::vec4 local_p0 = glm::vec4(-1,0,-1, 1);
            glm::vec4 local_p1 = glm::vec4(-1,0,1, 1);
            glm::vec4 local_p3 = glm::vec4(1,0,-1, 1);

            auto transform = sceneNode->WorldTransform();
                
            LightObject light;
            light.p0 = transform * local_p0;
            light.p1 = transform * local_p1;
            light.p3 = transform * local_p3;
            vec3 dir = vec3(transform * glm::vec4(0,1,0,0));
            light.normal_area = glm::vec4(glm::normalize(dir),0);
            light.normal_area.w = glm::length(glm::cross(glm::vec3(light.p1 - light.p0), glm::vec3(light.p3 - light.p0))) / 2.0f;
            
            out_lights.push_back(light);
        }
        
        // for each child node
        for (int child : node.children)
        {
            ParseGltfNode(out_nodes, nodeMap, out_camera, out_lights, model, child, modelIdx);
            nodeMap[child]->SetParent(sceneNode);
        }
    }

    static std::string currSceneName = "default";
    
    bool LoadImageData(tinygltf::Image * image, const int image_idx, std::string * err,
                   std::string * warn, int req_width, int req_height,
                   const unsigned char * bytes, int size, void * user_data )
    {
        image->as_is = true;
        return true;
    }
    
    bool Model::LoadGLTFScene(const std::string& filename, Assets::EnvironmentSetting& cameraInit, std::vector< std::shared_ptr<Assets::Node> >& nodes,
                              std::vector<Assets::Model>& models,
                              std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        int32_t matieralIdx = static_cast<int32_t>(materials.size());
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
                fmt::print("failed to load file: {}\n", filename);
                return false;
            }
            if(!gltfLoader.LoadBinaryFromMemory(&model, &err, &warn, data.data(), data.size()) )
            {
                fmt::print("failed to parse glb file: {}\n", filename);
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
                fmt::print("failed to parse glb file: {}\n", filename);
                return false;
            }
        }

        // delayed texture creation
        textureIdMap.resize(model.images.size(), -1);
        auto lambdaLoadTexture = [&textureIdMap, &model, filepath](int texture, bool srgb)
        {
            if (texture != -1)
            {
                int imageIdx = model.textures[texture].source;
                if (imageIdx == -1) imageIdx = texture;

                if (textureIdMap[imageIdx] != -1)
                {
                    return;
                }
                
                // create texture
                auto& image = model.images[imageIdx];
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
                if (imageIdx == -1) imageIdx = texture;
                return textureIdMap[imageIdx];
            }
            return -1;
        };
        
        for (tinygltf::Material& mat : model.materials)
        {
            lambdaLoadTexture(mat.pbrMetallicRoughness.baseColorTexture.index, true);
            lambdaLoadTexture(mat.pbrMetallicRoughness.metallicRoughnessTexture.index, false);
            lambdaLoadTexture(mat.normalTexture.index, false);
        }
        
        // load all materials
        for (tinygltf::Material& mat : model.materials)
        {
            Material m{};

            m.DiffuseTextureId = -1;
            m.MRATextureId = -1;
            m.NormalTextureId = -1;
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
                m.Metalness = 1.0f;
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
            if (emissive != mat.extensions.end())
            {
                float power = static_cast<float>(emissive->second.Get("emissiveStrength").GetNumberAsDouble());
                m = Material::DiffuseLight(emissiveColor * power * 100.0f);
            }

            materials.push_back( { mat.name, static_cast<uint32_t>(materials.size()), m } );
        }

        // export whole scene into a big buffer, with vertice indices materials
        for (tinygltf::Mesh& mesh : model.meshes)
        {
            bool hasTangent = false;
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            uint32_t vertext_offset = 0;
            uint32_t sectionIdx = 0;
            for (tinygltf::Primitive& primtive : mesh.primitives)
            {
                tinygltf::Accessor indexAccessor = model.accessors[primtive.indices];
                if( primtive.mode != TINYGLTF_MODE_TRIANGLES || indexAccessor.count == 0)
                {
                    continue;
                }
               
                tinygltf::Accessor positionAccessor = model.accessors[primtive.attributes["POSITION"]];
                tinygltf::Accessor normalAccessor = model.accessors[primtive.attributes["NORMAL"]];
                tinygltf::Accessor texcoordAccessor = model.accessors[primtive.attributes["TEXCOORD_0"]];

                tinygltf::Accessor tangentAccessor;
                tinygltf::BufferView tangentView;
                int tangentStride = 0;
                
                if(primtive.attributes.find("TANGENT") != primtive.attributes.end())
                {
                    hasTangent = true;

                    tangentAccessor = model.accessors[primtive.attributes["TANGENT"]];
                    tangentView = model.bufferViews[tangentAccessor.bufferView];
                    tangentStride = tangentAccessor.ByteStride(tangentView);
                }

                tinygltf::BufferView positionView = model.bufferViews[positionAccessor.bufferView];
                tinygltf::BufferView normalView = model.bufferViews[normalAccessor.bufferView];
                tinygltf::BufferView texcoordView = model.bufferViews[texcoordAccessor.bufferView];

                int positionStride = positionAccessor.ByteStride(positionView);
                int normalStride = normalAccessor.ByteStride(normalView);
                int texcoordStride = texcoordAccessor.ByteStride(texcoordView);
                
                for (size_t i = 0; i < positionAccessor.count; ++i)
                {
                    Vertex vertex;
                    float* position = reinterpret_cast<float*>(&model.buffers[positionView.buffer].data[positionView.byteOffset + positionAccessor.byteOffset + i *
                        positionStride]);
                    vertex.Position = vec3(
                        position[0],
                        position[1],
                        position[2]
                    );
                    float* normal = reinterpret_cast<float*>(&model.buffers[normalView.buffer].data[normalView.byteOffset + normalAccessor.byteOffset + i *
                        normalStride]);
                    vertex.Normal = vec3(
                        normal[0],
                        normal[1],
                        normal[2]
                    );

                    if(hasTangent)
                    {
                        float* tangent = reinterpret_cast<float*>(&model.buffers[tangentView.buffer].data[tangentView.byteOffset + tangentAccessor.byteOffset + i *
                       tangentStride]);
                        vertex.Tangent = vec4(
                            tangent[0],
                            tangent[1],
                            tangent[2],
                            tangent[3]
                        );
                    }

                    if(texcoordView.byteOffset + i *
                        texcoordStride < model.buffers[texcoordView.buffer].data.size())
                    {
                        float* texcoord = reinterpret_cast<float*>(&model.buffers[texcoordView.buffer].data[texcoordView.byteOffset + texcoordAccessor.byteOffset + i *
                  texcoordStride]);
                        vertex.TexCoord = vec2(
                            texcoord[0],
                            texcoord[1]
                        );              
                    }
                    
                    vertex.MaterialIndex = sectionIdx;
                    vertices.push_back(vertex);
                }
                
                sectionIdx++;
                tinygltf::BufferView indexView = model.bufferViews[indexAccessor.bufferView];
                int strideIndex = indexAccessor.ByteStride(indexView);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT )
                    {
                        uint16* data = reinterpret_cast<uint16*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                        indices.push_back(*data + vertext_offset);
                    }
                    else if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT )
                    {
                        uint32* data = reinterpret_cast<uint32*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                        indices.push_back(*data + vertext_offset);
                    }
                    else if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_INT )
                    {
                        int32* data = reinterpret_cast<int32*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                        indices.push_back(*data + vertext_offset);
                    }
                    else
                    {
                        assert(0);
                    }
                }

                vertext_offset += static_cast<uint32_t>(positionAccessor.count);
            }

            #if FLATTEN_VERTICE
                FlattenVertices(vertices, indices);
            #endif

            
            models.push_back(Assets::Model(std::move(vertices), std::move(indices), !hasTangent));
        }

        // default auto camera
        Camera defaultCam = Model::AutoFocusCamera(cameraInit, models);

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
        std::map<int, std::shared_ptr<Node> > nodeMap;
        for (int nodeIdx : model.scenes[0].nodes)
        {
            ParseGltfNode(nodes, nodeMap, cameraInit, lights, model, nodeIdx, modelIdx);
        }

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
                if (track.target_path == "scale")
                {
                    tinygltf::Accessor inputAccessor = model.accessors[animation.samplers[track.sampler].input];
                    tinygltf::Accessor outputAccessor = model.accessors[animation.samplers[track.sampler].output];

                    tinygltf::BufferView inputView = model.bufferViews[inputAccessor.bufferView];
                    tinygltf::BufferView outputView = model.bufferViews[outputAccessor.bufferView];

                    std::string nodeName = model.nodes[track.target_node].name;
                    AnimationTrack& CreateTrack = trackMaps[nodeName];

                    CreateTrack.NodeName_ = nodeName;
                    CreateTrack.Time_ = 0;

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

                        AnimationKey<glm::vec3> Key;
                        Key.Time = time;
                        Key.Value = translation;

                        CreateTrack.ScaleChannel.Keys.push_back(Key);
                        CreateTrack.Duration_ = max(time, CreateTrack.Duration_);
                    }
                }
                if (track.target_path == "rotation")
                {
                    tinygltf::Accessor inputAccessor = model.accessors[animation.samplers[track.sampler].input];
                    tinygltf::Accessor outputAccessor = model.accessors[animation.samplers[track.sampler].output];

                    tinygltf::BufferView inputView = model.bufferViews[inputAccessor.bufferView];
                    tinygltf::BufferView outputView = model.bufferViews[outputAccessor.bufferView];
                    
                    std::string nodeName = model.nodes[track.target_node].name;
                    AnimationTrack& CreateTrack = trackMaps[nodeName];

                    CreateTrack.NodeName_ = nodeName;
                    CreateTrack.Time_ = 0;

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

                        AnimationKey<glm::quat> Key;
                        Key.Time = time;
                        Key.Value = rotation;
                        
                        CreateTrack.RotationChannel.Keys.push_back(Key);
                        CreateTrack.Duration_ = max(time, CreateTrack.Duration_);
                    }
                }
                if (track.target_path == "translation")
                {
                    tinygltf::Accessor inputAccessor = model.accessors[animation.samplers[track.sampler].input];
                    tinygltf::Accessor outputAccessor = model.accessors[animation.samplers[track.sampler].output];

                    tinygltf::BufferView inputView = model.bufferViews[inputAccessor.bufferView];
                    tinygltf::BufferView outputView = model.bufferViews[outputAccessor.bufferView];
                    
                    std::string nodeName = model.nodes[track.target_node].name;
                    AnimationTrack& CreateTrack = trackMaps[nodeName];
                    
                    CreateTrack.NodeName_ = nodeName;
                    CreateTrack.Time_ = 0;

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

                        AnimationKey<glm::vec3> Key;
                        Key.Time = time;
                        Key.Value = translation;

                        CreateTrack.TranslationChannel.Keys.push_back(Key);
                        CreateTrack.Duration_ = max(time, CreateTrack.Duration_);
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
            cameraInit.cameras[i].FieldOfView = static_cast<float>(cam.perspective.yfov) * 180.f / M_PI;
            
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

    template <typename T>
    T AnimationChannel<T>::Sample(float time)
    {
        for ( int i = 0; i < Keys.size() - 1; i++ )
        {
            auto& Key = Keys[i];
            auto& KeyNext = Keys[i + 1];
            if (time >= Key.Time && time < KeyNext.Time)
            {
                float t = (time - Key.Time) / (KeyNext.Time - Key.Time);
                return glm::mix(Key.Value, KeyNext.Value, t);
            }

            if ( i == 0 && time < Key.Time )
            {
                return Key.Value;
            }

            if ( i == Keys.size() - 2)
            {
                return KeyNext.Value;
            }
        }
        return T{};
    }

    // 偏特化T == glm::quat
    template <>
    glm::quat AnimationChannel<glm::quat>::Sample(float time)
    {
        for ( int i = 0; i < Keys.size() - 1; i++ )
        {
            auto& Key = Keys[i];
            auto& KeyNext = Keys[i + 1];
            if (time >= Key.Time && time < KeyNext.Time)
            {
                float t = (time - Key.Time) / (KeyNext.Time - Key.Time);
                return glm::slerp(Key.Value, KeyNext.Value, t);
            }

            if ( i == 0 && time < Key.Time )
            {
                return Key.Value;
            }

            if ( i == Keys.size() - 2)
            {
                return KeyNext.Value;
            }
        }
        return {};
    }
    
    void AnimationTrack::Sample(float time, glm::vec3& translation, glm::quat& rotation, glm::vec3& scaling)
    {
        if (TranslationChannel.Keys.size() > 0)
        {
            translation = TranslationChannel.Sample(time);
        }
        if (RotationChannel.Keys.size() > 0)
        {
            rotation = RotationChannel.Sample(time);
        }
        if (ScaleChannel.Keys.size() > 0)
        {
            scaling = ScaleChannel.Sample(time);
        }
    }

    void Model::FlattenVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
    {
        // TODO: change to use povoking vertex later
        bool doFlatten = true;//(GOption->RendererType == 1 || GOption->RendererType == 2);

        if(doFlatten) {
            std::vector<Vertex> vertices_flatten;
            std::vector<uint32_t> indices_flatten;

            uint32_t idx_counter = 0;
            for (uint32_t index : indices)
            {
                if (index < 0 || index > vertices.size() - 1) continue; //fix "out of range index" error

                vertices_flatten.push_back(vertices[index]);
                indices_flatten.push_back(idx_counter++);
            }

            vertices = std::move(vertices_flatten);
            indices = std::move(indices_flatten);
        }
    }

    Camera Model::AutoFocusCamera(Assets::EnvironmentSetting& cameraInit, std::vector<Model>& models)
    {
        //auto center camera by scene bounds
        glm::vec3 boundsMin, boundsMax;
        for (int i = 0; i < models.size(); i++)
        {
            auto& model = models[i];
            glm::vec3 AABBMin = model.GetLocalAABBMin();
            glm::vec3 AABBMax = model.GetLocalAABBMax();
            if (i == 0)
            {
                boundsMin = AABBMin;
                boundsMax = AABBMax;
            }
            else
            {
                boundsMin = glm::min(AABBMin, boundsMin);
                boundsMax = glm::max(AABBMax, boundsMax);
            }
        }

        glm::vec3 boundsCenter = (boundsMax - boundsMin) * 0.5f + boundsMin;

        Camera newCamera;
        newCamera.ModelView = lookAt(vec3(boundsCenter.x, boundsCenter.y, boundsCenter.z + glm::length(boundsMax - boundsMin)), boundsCenter, vec3(0, 1, 0));
        newCamera.FieldOfView = 40;
        newCamera.Aperture = 0.0f;

        return newCamera;
    }

    uint32_t Model::CreateCornellBox(const float scale,
                                 std::vector<Model>& models,
                                 std::vector<FMaterial>& materials,
                                 std::vector<LightObject>& lights)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        CornellBox::Create(scale, vertices, indices, materials, lights);
#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif
        models.push_back(Model(
            std::move(vertices),
            std::move(indices),
            true
        ));

        return models.size() - 1;
    }

    Model Model::CreateBox(const vec3& p0, const vec3& p1)
    {
        std::vector<Vertex> vertices =
        {
            Vertex{vec3(p0.x, p0.y, p0.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p0.y, p1.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(-1, 0, 0), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p1.x, p0.y, p1.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p0.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p0.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(1, 0, 0), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p1.x, p0.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p0.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p0.z), vec3(0, 0, -1), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p0.x, p0.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(0, 0, 1), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p0.x, p0.y, p0.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p0.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p0.y, p1.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p0.y, p1.z), vec3(0, -1, 0), vec4(1,0,0,0), vec2(0), 0},

            Vertex{vec3(p1.x, p1.y, p0.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(0, 1, 0), vec4(1,0,0,0), vec2(0), 0},
        };

        std::vector<uint32_t> indices =
        {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23
        };

#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif

        return Model( std::move(vertices),std::move(indices), true);
    }

    Model Model::CreateSphere(const vec3& center, float radius)
    {
        const int slices = 32;
        const int stacks = 16;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const float j0_delta = M_PI / static_cast<float>(stacks);
        float j0 = 0.f;

        const float i0_delta = (M_PI + M_PI) / static_cast<float>(slices);
        float i0;

        for (int j = 0; j <= stacks; ++j)
        {
            // Vertex
            const float v = radius * -std::sin(j0);
            const float z = radius * std::cos(j0);

            // Normals		
            const float n0 = -std::sin(j0);
            const float n1 = std::cos(j0);

            i0 = 0;

            for (int i = 0; i <= slices; ++i)
            {
                const vec3 position(
                    center.x + v * std::sin(i0),
                    center.y + z,
                    center.z + v * std::cos(i0));

                const vec3 normal(
                    n0 * std::sin(i0),
                    n1,
                    n0 * std::cos(i0));

                const vec2 texCoord(
                    static_cast<float>(i) / slices,
                    static_cast<float>(j) / stacks);

                vertices.push_back(Vertex{position, normal, vec4(1,0,0,0), texCoord, 0});

                i0 += i0_delta;
            }

            j0 += j0_delta;
        }

        {
            int slices1 = slices + 1;
            int j0 = 0;
            int j1 = slices1;
            for (int j = 0; j < stacks; ++j)
            {
                for (int i = 0; i < slices; ++i)
                {
                    const auto i0 = i;
                    const auto i1 = i + 1;

                    indices.push_back(j0 + i0);
                    indices.push_back(j1 + i0);
                    indices.push_back(j1 + i1);

                    indices.push_back(j0 + i0);
                    indices.push_back(j1 + i1);
                    indices.push_back(j0 + i1);
                }

                j0 += slices1;
                j1 += slices1;
            }
        }
#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif

        return Model(std::move(vertices), std::move(indices));
    }
    
    uint32_t Model::CreateLightQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                const glm::vec3& dir, const glm::vec3& lightColor,
                                std::vector<Model>& models,
                                std::vector<Material>& materials,
                                std::vector<LightObject>& lights)
    {
        materials.push_back(Material::DiffuseLight(lightColor));
        uint32_t materialIdx = materials.size() - 1;
        
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        vertices.push_back(Vertex{p0, dir, vec4(1,0,0,0),vec2(0, 1), materialIdx});
        vertices.push_back(Vertex{p1, dir, vec4(1,0,0,0),vec2(1, 1), materialIdx});
        vertices.push_back(Vertex{p2, dir, vec4(1,0,0,0),vec2(1, 0), materialIdx});
        vertices.push_back(Vertex{p3, dir, vec4(1,0,0,0),vec2(0, 0), materialIdx});

        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);

        indices.push_back(0);
        indices.push_back(2);
        indices.push_back(3);
        
        LightObject light;
        light.p0 = vec4(p0,1);
        light.p1 = vec4(p1,1);
        light.p3 = vec4(p3,1);
        light.normal_area = vec4(dir, 0);
        light.normal_area.w = glm::length(glm::cross(glm::vec3(light.p1 - light.p0), glm::vec3(light.p3 - light.p0))) / 2.0f;
        
        lights.push_back(light);

#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif
        
        models.push_back( Model(
            std::move(vertices),
            std::move(indices)));

        return static_cast<int32_t>(models.size()) - 1;
    }

    void Model::FreeMemory()
    {
        vertices_ = std::vector<Vertex>();
        indices_ = std::vector<uint32_t>();
    }

    Model::Model(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, bool needGenTSpace) :
        vertices_(std::move(vertices)),
        indices_(std::move(indices))
    {
        verticeCount = vertices_.size();
        indiceCount = indices_.size();
        
        // calculate local aabb
        local_aabb_min = glm::vec3(999999, 999999, 999999);
        local_aabb_max = glm::vec3(-999999, -999999, -999999);

        for( const auto& vertex : vertices_ )
        {
            local_aabb_min = glm::min(local_aabb_min, vertex.Position);
            local_aabb_max = glm::max(local_aabb_max, vertex.Position);
        }

        if(needGenTSpace)
        {
            GenerateMikkTSpace(this);
        }
    }

    std::shared_ptr<Node> Node::CreateNode(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t id, uint32_t instanceId, bool replace)
    {
        return std::make_shared<Node>(name, translation, rotation, scale, id, instanceId, replace);
    }

    void Node::SetTranslation(glm::vec3 translation)
    {
        translation_ = translation;
    }

    void Node::SetRotation(glm::quat rotation)
    {
        rotation_ = rotation;
    }

    void Node::SetScale(glm::vec3 scale)
    {
        scaling_ = scale;
    }

    void Node::RecalcLocalTransform()
    {
        localTransform_ = glm::translate(glm::mat4(1), translation_) * glm::mat4_cast(rotation_) * glm::scale(glm::mat4(1), scaling_);
    }

    void Node::RecalcTransform(bool full)
    {
        RecalcLocalTransform();
        if(parent_)
        {
            transform_ = parent_->transform_ * localTransform_;
        }
        else
        {
            transform_ = localTransform_;
        }

        // update children
        if (full)
        {
            for(auto& child : children_)
            {
                child->RecalcTransform(full);
            }
        }
    }

    bool Node::TickVelocity(glm::mat4& combinedTS)
    {
        if (!physicsBodyTemp_.IsInvalid())
        {
            auto body = NextEngine::GetInstance()->GetPhysicsEngine()->GetBody(physicsBodyTemp_);
            if (body != nullptr)
            {
                SetTranslation(body->position);
                RecalcTransform(true);
            }
        }
        
        combinedTS = prevTransform_ * glm::inverse(transform_);
        prevTransform_ = transform_;

        glm::vec3 newPos = combinedTS * glm::vec4(0,0,0,1);
        return length2(newPos) > 0.1;
    }

    void Node::SetParent(std::shared_ptr<Node> parent)
    {
        // remove form previous parent
        if(parent_)
        {
            parent_->RemoveChild( shared_from_this() );
        }
        parent_ = parent;
        parent_->AddChild( shared_from_this() );

        RecalcTransform();
    }

    void Node::AddChild(std::shared_ptr<Node> child)
    {
        children_.insert(child);
    }

    void Node::RemoveChild(std::shared_ptr<Node> child)
    {
        children_.erase(child);
    }

    void Node::SetMaterial(const std::vector<uint32_t>& materials)
    {
        materialIdx_ = materials;
    }

    NodeProxy Node::GetNodeProxy() const
    {
        NodeProxy proxy;
        proxy.instanceId = instanceId_;
        proxy.modelId = modelId_;
        proxy.worldTS = WorldTransform();
        for ( int i = 0; i < materialIdx_.size(); i++ )
        {
            if (i < 16)
            {
                proxy.matId[i] = materialIdx_[i];
            }
        }

        return proxy;
    }

    Node::Node(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t id, uint32_t instanceId, bool replace):
    name_(name),
    translation_(translation), rotation_(rotation), scaling_(scale), 
    modelId_(id), instanceId_(instanceId), visible_(false)
    {
        RecalcLocalTransform();
        RecalcTransform();
        if(replace)
        {
            prevTransform_ = transform_;
        }
        else
        {
            prevTransform_ = translate(glm::mat4(1), vec3(0,-100,0));
        }
    }
}
