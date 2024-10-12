#include "Model.hpp"
#include "CornellBox.hpp"
#include "Procedural.hpp"
#include "Sphere.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Math.hpp"

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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include <fmt/format.h>

#include "Options.hpp"
#include "Texture.hpp"

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
    void ParseGltfNode(std::vector<Assets::Node>& out_nodes, Assets::CameraInitialSate& out_camera, std::vector<Assets::LightObject>& out_lights,
        glm::mat4 parentTransform, tinygltf::Model& model, int node_idx, int modelIdx)
    {
        tinygltf::Node& node = model.nodes[node_idx];

        glm::mat4 transform = glm::mat4(1);
        if( node.matrix.empty() )
        {
            glm::vec3 translation = node.translation.empty()
                               ? glm::vec3(0)
                               : glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            glm::vec3 scaling = node.scale.empty() ? glm::vec3(1) : glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
            glm::quat quaternion = node.rotation.empty()
                                       ? glm::quat(1, 0, 0, 0)
                                       : glm::quat(
                                           static_cast<float>(node.rotation[3]),
                                           static_cast<float>(node.rotation[0]),
                                           static_cast<float>(node.rotation[1]),
                                           static_cast<float>(node.rotation[2]));
            quaternion = glm::normalize(quaternion);
            glm::mat4 t = glm::translate(glm::mat4(1), translation);
            glm::mat4 r = glm::toMat4(quaternion);
            glm::mat4 s = glm::scale(glm::mat4(1), scaling);
        
            transform =  parentTransform * (t * r * s);   
        }
        else
        {
            glm::mat4 localTs = glm::make_mat4(node.matrix.data());
            transform =  parentTransform * localTs;   
        }

        if(node.mesh != -1)
        {
            if( node.extras.Has("arealight") )
            {
                out_nodes.push_back(Node::CreateNode(node.name, transform, node.mesh + modelIdx, false));

                // use the aabb to build a light, using the average normals and area
                // the basic of lightquad from blender is a 2 x 2 quad ,from -1 to 1
                glm::vec4 local_p0 = glm::vec4(-1,0,-1, 1);
                glm::vec4 local_p1 = glm::vec4(-1,0,1, 1);
                glm::vec4 local_p3 = glm::vec4(1,0,-1, 1);
                
                LightObject light;
                light.p0 = transform * local_p0;
                light.p1 = transform * local_p1;
                light.p3 = transform * local_p3;
                vec3 dir = vec3(transform * glm::vec4(0,1,0,0));
                light.normal_area = glm::vec4(glm::normalize(dir),0);
                light.normal_area.w = glm::length(glm::cross(glm::vec3(light.p1 - light.p0), glm::vec3(light.p3 - light.p0))) / 2.0f;
                
                out_lights.push_back(light);
            }
            else
            {
                out_nodes.push_back(Node::CreateNode(node.name, transform, node.mesh + modelIdx, false));
            }
        }
        else
        {
            if(node.camera >= 0)
            {
                vec4 camEye = transform * glm::vec4(0,0,0,1);
                vec4 camFwd = transform * glm::vec4(0,0,-1,0);
                glm::mat4 ModelView = lookAt(vec3(camEye), vec3(camEye) + vec3(camFwd.x, camFwd.y, camFwd.z), glm::vec3(0, 1, 0));
                out_camera.cameras.push_back({ std::to_string(node.camera) + " " + node.name, ModelView, 40});

                if(node.camera == 0) out_camera.ModelView = ModelView;
            }
        }

        for ( int child : node.children )
        {
            ParseGltfNode(out_nodes, out_camera, out_lights, transform, model, child, modelIdx);
        }
    }
    
    void Model::LoadGLTFScene(const std::string& filename, Assets::CameraInitialSate& cameraInit, std::vector<Assets::Node>& nodes,
                              std::vector<Assets::Model>& models,
                              std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights)
    {
        int32_t matieralIdx = static_cast<int32_t>(materials.size());
        int32_t modelIdx = static_cast<int32_t>(models.size());
        
        tinygltf::Model model;
        tinygltf::TinyGLTF gltfLoader;
        std::string err;
        std::string warn;

        if(!gltfLoader.LoadBinaryFromFile(&model, &err, &warn, filename) )
        {
            return;
        }

        // load all textures
        std::vector<uint32_t> textureIdMap;

        std::filesystem::path filepath = filename;
        
        for ( uint32_t i = 0; i < model.images.size(); ++i )
        {
            tinygltf::Image& image = model.images[i];
            
            std::string texname = image.name.empty() ? fmt::format("tex_{}", i):  image.name;
            // 假设，这里的image id和外面的textures id是一样的
            uint32_t texIdx = GlobalTexturePool::LoadTexture(
                filepath.filename().string() + "_" + texname, model.buffers[0].data.data() + model.bufferViews[image.bufferView].byteOffset,
                model.bufferViews[image.bufferView].byteLength, Vulkan::SamplerConfig());

            textureIdMap.push_back(texIdx);
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
            
            int texture = mat.pbrMetallicRoughness.baseColorTexture.index;
            if(texture != -1)
            {
                m.DiffuseTextureId = textureIdMap[ model.textures[texture].source ];
            }
            int mraTexture = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
            if(mraTexture != -1)
            {
                m.MRATextureId = textureIdMap[ model.textures[mraTexture].source ];
                m.Fuzziness = 1.0;
            }
            int normalTexture = mat.normalTexture.index;
            if(normalTexture != -1)
            {
                m.NormalTextureId = textureIdMap[ model.textures[normalTexture].source ];
                m.NormalTextureScale = static_cast<float>(mat.normalTexture.scale);
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

            if (m.Metalness > .95)
            {
                m.MaterialModel = Material::Enum::Metallic;
            }

            if (m.Fuzziness > .95 && m.MRATextureId == -1)
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

            materials.push_back(m);
        }

        // export whole scene into a big buffer, with vertice indices materials
        for (tinygltf::Mesh& mesh : model.meshes)
        {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<uint32_t> materials;

            uint32_t vertext_offset = 0;
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


                    vertex.MaterialIndex = max(0, primtive.material) + matieralIdx;
                    vertices.push_back(vertex);
                }

                materials.push_back(max(0, primtive.material) + matieralIdx);
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
            
            models.push_back(Assets::Model(std::move(vertices), std::move(indices), std::move(materials), nullptr));
        }

        // default auto camera
        Model::AutoFocusCamera(cameraInit, models);
        cameraInit.FieldOfView = 20;
        cameraInit.Aperture = 0.0f;
        cameraInit.FocusDistance = 1000.0f;
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = false;
        cameraInit.SkyIdx = 0;

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
        for (int nodeIdx : model.scenes[0].nodes)
        {
            ParseGltfNode(nodes, cameraInit, lights, glm::mat4(1), model, nodeIdx, modelIdx);
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
            
            if (i == 0) //use 1st camera params
            {
                cameraInit.FieldOfView = cameraInit.cameras[i].FieldOfView;
                cameraInit.Aperture = cameraInit.cameras[i].Aperture;
                cameraInit.FocusDistance = cameraInit.cameras[i].FocalDistance;
                cameraInit.CameraIdx = 0;

            }
            i++;
        }
        //printf("model.cameras: %d\n", i);
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

    void Model::AutoFocusCamera(Assets::CameraInitialSate& cameraInit, std::vector<Model>& models)
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
        cameraInit.ModelView = lookAt(vec3(boundsCenter.x, boundsCenter.y, boundsCenter.z + glm::length(boundsMax - boundsMin)), boundsCenter, vec3(0, 1, 0));
    }

    int Model::LoadObjModel(const std::string& filename, std::vector<Node>& nodes, std::vector<Model>& models,
                            std::vector<Material>& materials,
                            std::vector<LightObject>& lights, bool autoNode)
    {
        int32_t materialIdxOffset = static_cast<int32_t>(materials.size());
        
        fmt::print("- loading '{}'... \n", filename);

        const auto timer = std::chrono::high_resolution_clock::now();
        const std::string materialPath = std::filesystem::path(filename).parent_path().string();

        tinyobj::ObjReader objReader;
        std::vector<std::string> searchPaths;
        searchPaths.push_back(Utilities::FileHelper::GetPlatformFilePath("assets/textures/"));
		searchPaths.push_back(materialPath + "/");

        if (!objReader.ParseFromFile(filename))
        {
            Throw(std::runtime_error("failed to load model '" + filename + "':\n" + objReader.Error()));
        }

        if (!objReader.Warning().empty())
        {
            Utilities::Console::Write(Utilities::Severity::Warning, [&objReader]()
            {
                fmt::print("\nWARNING: {}\n", objReader.Warning());
            });
        }

        bool file_exists;
        std::string loadname, fn;

        for (const auto& _material : objReader.GetMaterials())
        {
            tinyobj::material_t material = _material;
            Material m{};

            // texture stuff
            m.DiffuseTextureId = -1;
            if (material.diffuse_texname != "")
            {
                material.diffuse[0] = 1.0f;
                material.diffuse[1] = 1.0f;
                material.diffuse[2] = 1.0f;

				file_exists = false;
                // find if textures contain texture with loadname equals diffuse_texname
				for(size_t i=0; i< searchPaths.size() && !file_exists; i++) {
					fn = searchPaths[i] + material.diffuse_texname;
					file_exists = std::filesystem::exists(fn);
					if(file_exists) loadname = fn;
				}				

                if(file_exists) {
                	loadname = std::filesystem::canonical(loadname).generic_string();
                	m.DiffuseTextureId = GlobalTexturePool::LoadTexture(loadname, Vulkan::SamplerConfig());
                } else {
                	fmt::print("\n{} NOT FOUND\n", material.diffuse_texname);
				}
            }

            m.Diffuse = vec4(material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0);

            m.MaterialModel = Material::Enum::Mixture;
            m.Fuzziness = material.roughness;
            m.RefractionIndex = m.RefractionIndex2 = max(material.ior, 1.43f);
            m.Metalness = material.metallic * material.metallic;

            if (material.name == "Window-Fake-Glass" || material.name == "Wine-Glasses" || material.name.find("Water")
                != std::string::npos || material.name.find("Glass") != std::string::npos || material.name.find("glass")
                != std::string::npos)
            {
                m.MaterialModel = Material::Enum::Dielectric;
            }

            if (material.emission[0] + material.emission[1] + material.emission[2] > 0)
            {
                m = Material::DiffuseLight(vec3(material.emission[0], material.emission[1], material.emission[2]) * 100.f);
                // add to lights
            }

            if (material.metallic > .99f)
            {
                m.MaterialModel = Material::Enum::Metallic;
            }

            materials.emplace_back(m);
        }

        if (materialIdxOffset == static_cast<int32_t>(materials.size()))
        {
            Material m{};

            m.Diffuse = vec4(0.7f, 0.7f, 0.7f, 1.0);
            m.DiffuseTextureId = -1;

            materials.emplace_back(m);

            materialIdxOffset = static_cast<int32_t>(materials.size());
        }

        // Geometry
        const auto& objAttrib = objReader.GetAttrib();
        std::unordered_map<Vertex, uint32_t> uniqueVertices(objAttrib.vertices.size());

        // add Geometry one by one
        for (const auto& shape : objReader.GetShapes())
        {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<uint32_t> materials;

            const auto& mesh = shape.mesh;
            size_t faceId = 0;
            for (const auto& index : mesh.indices)
            {
                Vertex vertex = {};

                size_t idx = 3 * index.vertex_index;

                vertex.Position =
                {
                    objAttrib.vertices[idx],
                    objAttrib.vertices[idx + 1],
                    objAttrib.vertices[idx + 2],
                };

                if (!objAttrib.normals.empty())
                {
                    idx = 3 * index.normal_index;
                    vertex.Normal =
                    {
                        objAttrib.normals[idx],
                        objAttrib.normals[idx + 1],
                        objAttrib.normals[idx + 2]
                    };
                }

                if (!objAttrib.texcoords.empty())
                {
                    vertex.TexCoord =
                    {
                        objAttrib.texcoords[2 * max(0, index.texcoord_index) + 0],
                        1 - objAttrib.texcoords[2 * max(0, index.texcoord_index) + 1]
                    };
                }

                vertex.MaterialIndex = std::max(0, mesh.material_ids[faceId++ / 3] + materialIdxOffset);

                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                if( std::find(materials.begin(), materials.end(), vertex.MaterialIndex) == std::end(materials) )
                {
                    materials.push_back(vertex.MaterialIndex);
                }
                
                indices.push_back(uniqueVertices[vertex]);
            }

            // If the model did not specify normals, then create smooth normals that conserve the same number of vertices.
            // Using flat normals would mean creating more vertices than we currently have, so for simplicity and better visuals we don't do it.
            // See https://stackoverflow.com/questions/12139840/obj-file-averaging-normals.
            if (objAttrib.normals.empty())
            {
                std::vector<vec3> normals(vertices.size());

                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    const auto normal = normalize(cross(
                        vec3(vertices[indices[i + 1]].Position) - vec3(vertices[indices[i]].Position),
                        vec3(vertices[indices[i + 2]].Position) - vec3(vertices[indices[i]].Position)));

                    vertices[indices[i]].Normal += normal;
                    vertices[indices[i + 1]].Normal += normal;
                    vertices[indices[i + 2]].Normal += normal;
                }

                for (auto& vertex : vertices)
                {
                    vertex.Normal = normalize(vertex.Normal);
                }
            }

            if(vertices.size() == 0)
            {
                continue;
            }
            // flatten the vertice and indices, individual vertice
#if FLATTEN_VERTICE
            FlattenVertices(vertices, indices);
#endif

            models.push_back(Model(std::move(vertices), std::move(indices), std::move(materials), nullptr));
            if(autoNode)
            {
                nodes.push_back(Node::CreateNode(Utilities::NameHelper::RandomName(6), mat4(1), static_cast<int>(models.size()) - 1, false));
            }
        }
        
        const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
            std::chrono::high_resolution_clock::now() - timer).count();

        fmt::print("{} vertices, {} unique vertices, {} materials, {} lights\n{:.1f}s\n", 
                    Utilities::metricFormatter(static_cast<double>(objAttrib.vertices.size()), ""),
					Utilities::metricFormatter(static_cast<double>(uniqueVertices.size()), ""),
					materials.size(), lights.size(), elapsed);

        return static_cast<int32_t>(models.size()) - 1;
    }


    uint32_t Model::CreateCornellBox(const float scale,
                                 std::vector<Model>& models,
                                 std::vector<Material>& materials,
                                 std::vector<LightObject>& lights)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<uint32_t> materialIds;

        uint32_t prev_mat_id = materials.size();

        materialIds.push_back(prev_mat_id + 0);
        materialIds.push_back(prev_mat_id + 1);
        materialIds.push_back(prev_mat_id + 2);
        materialIds.push_back(prev_mat_id + 3);

        CornellBox::Create(scale, vertices, indices, materials, lights);

#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif

        models.push_back(Model(
            std::move(vertices),
            std::move(indices),
            std::move(materialIds),
            nullptr
        ));

        return models.size() - 1;
    }

    Model Model::CreateBox(const vec3& p0, const vec3& p1, uint32_t materialIdx)
    {
        std::vector<Vertex> vertices =
        {
            Vertex{vec3(p0.x, p0.y, p0.z), vec3(-1, 0, 0), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p0.y, p1.z), vec3(-1, 0, 0), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(-1, 0, 0), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(-1, 0, 0), vec2(0), materialIdx},

            Vertex{vec3(p1.x, p0.y, p1.z), vec3(1, 0, 0), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p0.y, p0.z), vec3(1, 0, 0), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p1.y, p0.z), vec3(1, 0, 0), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(1, 0, 0), vec2(0), materialIdx},

            Vertex{vec3(p1.x, p0.y, p0.z), vec3(0, 0, -1), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p0.y, p0.z), vec3(0, 0, -1), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(0, 0, -1), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p1.y, p0.z), vec3(0, 0, -1), vec2(0), materialIdx},

            Vertex{vec3(p0.x, p0.y, p1.z), vec3(0, 0, 1), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p0.y, p1.z), vec3(0, 0, 1), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(0, 0, 1), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(0, 0, 1), vec2(0), materialIdx},

            Vertex{vec3(p0.x, p0.y, p0.z), vec3(0, -1, 0), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p0.y, p0.z), vec3(0, -1, 0), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p0.y, p1.z), vec3(0, -1, 0), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p0.y, p1.z), vec3(0, -1, 0), vec2(0), materialIdx},

            Vertex{vec3(p1.x, p1.y, p0.z), vec3(0, 1, 0), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p1.y, p0.z), vec3(0, 1, 0), vec2(0), materialIdx},
            Vertex{vec3(p0.x, p1.y, p1.z), vec3(0, 1, 0), vec2(0), materialIdx},
            Vertex{vec3(p1.x, p1.y, p1.z), vec3(0, 1, 0), vec2(0), materialIdx},
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

        std::vector<uint32_t> materialids = {(uint32_t)materialIdx};

#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif

        return Model(
            std::move(vertices),
            std::move(indices),
            std::move(materialids),
            nullptr);
    }

    Model Model::CreateSphere(const vec3& center, float radius, uint32_t materialIdx, const bool isProcedural)
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

                vertices.push_back(Vertex{position, normal, texCoord, materialIdx});

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

        std::vector<uint32_t> materialIdxs = {(uint32_t)materialIdx};

#if FLATTEN_VERTICE
        FlattenVertices(vertices, indices);
#endif

        return Model(
            std::move(vertices),
            std::move(indices),
            std::move(materialIdxs),
            isProcedural ? new Sphere(center, radius) : nullptr);
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
        
        vertices.push_back(Vertex{p0, dir, vec2(0, 1), materialIdx});
        vertices.push_back(Vertex{p1, dir, vec2(1, 1), materialIdx});
        vertices.push_back(Vertex{p2, dir, vec2(1, 0), materialIdx});
        vertices.push_back(Vertex{p3, dir, vec2(0, 0), materialIdx});

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

        std::vector<uint32_t> materialIds = {(uint32_t)materialIdx};
        
        models.push_back( Model(
            std::move(vertices),
            std::move(indices),
            std::move(materialIds),
            nullptr));

        return static_cast<int32_t>(models.size()) - 1;
    }

    Model::Model(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, std::vector<uint32_t>&& materials,
                 const class Procedural* procedural) :
        vertices_(std::move(vertices)),
        indices_(std::move(indices)),
        materialIdx_(std::move(materials)),
        procedural_(procedural)
    {
        // calculate local aabb
        local_aabb_min = glm::vec3(999999, 999999, 999999);
        local_aabb_max = glm::vec3(-999999, -999999, -999999);

        for( const auto& vertex : vertices_ )
        {
            local_aabb_min = glm::min(local_aabb_min, vertex.Position);
            local_aabb_max = glm::max(local_aabb_max, vertex.Position);
        }
    }

    Node Node::CreateNode(std::string name, glm::mat4 transform, int id, bool procedural)
    {
        return Node(name, transform, id, procedural);
    }

    Node::Node(std::string name, glm::mat4 transform, int id, bool procedural): name_(name), transform_(transform), modelId_(id),
                                                              procedural_(procedural)
    {
    }
}
