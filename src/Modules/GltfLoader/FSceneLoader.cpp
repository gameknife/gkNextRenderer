#include "Modules/GltfLoader/FSceneLoader.h"
#include "Engine/Common/CoreMinimal.hpp"
#include <cfloat>

#include "Engine/Assets/Loaders/LoaderUtils.hpp"

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
#include <tiny_gltf.h>

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Core/LightObject.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"

namespace Assets
{
    static glm::vec4 GetAttributeValue(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index);

    namespace
    {
        uint32_t ReadIndex(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
        {
            const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
            const unsigned char* data = model.buffers[view.buffer].data.data() + view.byteOffset + accessor.byteOffset +
                                        index * accessor.ByteStride(view);
            switch (accessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return *reinterpret_cast<const uint8_t*>(data);
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return *reinterpret_cast<const uint16_t*>(data);
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return *reinterpret_cast<const uint32_t*>(data);
            default:
                return std::numeric_limits<uint32_t>::max();
            }
        }

        bool IsPureEmissivePrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive)
        {
            if (primitive.material < 0 || primitive.material >= static_cast<int>(model.materials.size()))
            {
                return false;
            }
            const tinygltf::Material& material = model.materials[primitive.material];
            if (material.emissiveTexture.index >= 0 || material.emissiveFactor.size() < 3)
            {
                return false;
            }
            const glm::dvec3 emissive(material.emissiveFactor[0], material.emissiveFactor[1],
                                      material.emissiveFactor[2]);
            return glm::length(emissive) > 0.001;
        }

        bool TryBuildAreaLight(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
                               const glm::mat4& worldTransform, int materialOffset, LightObject& outLight)
        {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES || primitive.indices < 0 ||
                primitive.attributes.find("POSITION") == primitive.attributes.end())
            {
                return false;
            }

            const tinygltf::Accessor& positionAccessor =
                model.accessors[primitive.attributes.at("POSITION")];
            const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
            if (positionAccessor.count != 4 || indexAccessor.count != 6)
            {
                return false;
            }

            std::array<uint32_t, 3> firstTriangle = {
                ReadIndex(model, indexAccessor, 0),
                ReadIndex(model, indexAccessor, 1),
                ReadIndex(model, indexAccessor, 2),
            };
            std::array<uint32_t, 3> secondTriangle = {
                ReadIndex(model, indexAccessor, 3),
                ReadIndex(model, indexAccessor, 4),
                ReadIndex(model, indexAccessor, 5),
            };

            std::vector<uint32_t> shared;
            uint32_t firstUnique = std::numeric_limits<uint32_t>::max();
            uint32_t secondUnique = std::numeric_limits<uint32_t>::max();
            for (uint32_t index : firstTriangle)
            {
                if (std::find(secondTriangle.begin(), secondTriangle.end(), index) != secondTriangle.end())
                {
                    shared.push_back(index);
                }
                else
                {
                    firstUnique = index;
                }
            }
            for (uint32_t index : secondTriangle)
            {
                if (std::find(firstTriangle.begin(), firstTriangle.end(), index) == firstTriangle.end())
                {
                    secondUnique = index;
                    break;
                }
            }
            if (shared.size() != 2 || firstUnique >= positionAccessor.count || secondUnique >= positionAccessor.count)
            {
                return false;
            }

            const uint32_t cornerIndex = shared.front();
            glm::vec3 p0 = glm::vec3(worldTransform * GetAttributeValue(model, positionAccessor, cornerIndex));
            glm::vec3 p1 = glm::vec3(worldTransform * GetAttributeValue(model, positionAccessor, firstUnique));
            glm::vec3 p3 = glm::vec3(worldTransform * GetAttributeValue(model, positionAccessor, secondUnique));

            glm::vec3 crossEdges = glm::cross(p1 - p0, p3 - p0);
            const float area = glm::length(crossEdges);
            if (!std::isfinite(area) || area <= 1.0e-8f)
            {
                return false;
            }

            const auto normalAttribute = primitive.attributes.find("NORMAL");
            if (normalAttribute != primitive.attributes.end())
            {
                const tinygltf::Accessor& normalAccessor = model.accessors[normalAttribute->second];
                if (normalAccessor.count > 0)
                {
                    const uint32_t normalIndex =
                        std::min<uint32_t>(cornerIndex, static_cast<uint32_t>(normalAccessor.count - 1));
                    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));
                    const glm::vec3 expectedNormal =
                        glm::normalize(normalMatrix * glm::vec3(GetAttributeValue(model, normalAccessor, normalIndex)));
                    if (glm::dot(crossEdges, expectedNormal) < 0.0f)
                    {
                        std::swap(p1, p3);
                        crossEdges = -crossEdges;
                    }
                }
            }

            outLight = {};
            outLight.p0 = glm::vec4(p0, 1.0f);
            outLight.p1 = glm::vec4(p1, 1.0f);
            outLight.p3 = glm::vec4(p3, 1.0f);
            outLight.normal_area = glm::vec4(glm::normalize(crossEdges), area);
            outLight.lightMatIdx = static_cast<uint32_t>(primitive.material + materialOffset);
            outLight.lightType = LightTypeArea;
            return true;
        }
    }

    std::string ReadSceneReferenceAssetPath(const tinygltf::Node& node)
    {
        if (!node.extras.Has("gkSceneReference"))
        {
            return {};
        }

        const tinygltf::Value& value = node.extras.Get("gkSceneReference");
        if (value.IsString())
        {
            return value.Get<std::string>();
        }
        if (value.IsObject() && value.Has("asset") && value.Get("asset").IsString())
        {
            return value.Get("asset").Get<std::string>();
        }
        return {};
    }

    bool ReadBoolExtra(const tinygltf::Value& extras, const char* key, bool fallback)
    {
        if (!extras.Has(key))
        {
            return fallback;
        }
        const tinygltf::Value& value = extras.Get(key);
        if (value.IsBool())
        {
            return value.Get<bool>();
        }
        if (value.IsNumber())
        {
            return value.GetNumberAsInt() != 0;
        }
        return fallback;
    }

    float ReadFloatExtra(const tinygltf::Value& extras, const char* key, float fallback)
    {
        return extras.Has(key) && extras.Get(key).IsNumber()
            ? static_cast<float>(extras.Get(key).GetNumberAsDouble())
            : fallback;
    }

    glm::vec3 ReadColorExtra(const tinygltf::Value& extras, const char* key, const glm::vec3& fallback)
    {
        if (!extras.Has(key) || !extras.Get(key).IsArray())
        {
            return fallback;
        }
        const auto& values = extras.Get(key).Get<tinygltf::Value::Array>();
        if (values.size() < 3 || !values[0].IsNumber() || !values[1].IsNumber() || !values[2].IsNumber())
        {
            return fallback;
        }
        return {
            static_cast<float>(values[0].GetNumberAsDouble()),
            static_cast<float>(values[1].GetNumberAsDouble()),
            static_cast<float>(values[2].GetNumberAsDouble()),
        };
    }

    void ReadEnvironmentExtras(const tinygltf::Value& extras, Assets::EnvironmentSetting& environment)
    {
        if (extras.Has("SkyIdx") && extras.Get("SkyIdx").IsNumber())
        {
            environment.HasSky = true;
            environment.SkyIdx = extras.Get("SkyIdx").GetNumberAsInt();
        }
        if (extras.Has("SkyIntensity") && extras.Get("SkyIntensity").IsNumber())
        {
            environment.HasSky = true;
            environment.SkyIntensity = static_cast<float>(extras.Get("SkyIntensity").GetNumberAsDouble());
        }
        if (extras.Has("SkyRotation") && extras.Get("SkyRotation").IsNumber())
        {
            environment.HasSky = true;
            environment.SkyRotation = static_cast<float>(extras.Get("SkyRotation").GetNumberAsDouble());
        }
        environment.SkyColor = ReadColorExtra(extras, "SkyColor", environment.SkyColor);
        if (extras.Has("SunIntensity") && extras.Get("SunIntensity").IsNumber())
        {
            environment.HasSun = true;
            environment.SunIntensity = static_cast<float>(extras.Get("SunIntensity").GetNumberAsDouble());
        }
        if (extras.Has("SunRotation") && extras.Get("SunRotation").IsNumber())
        {
            environment.SunRotation = static_cast<float>(extras.Get("SunRotation").GetNumberAsDouble());
        }
        environment.SunElevation =
            ReadFloatExtra(extras, "SunElevation", environment.SunElevation);
        environment.SunColor = ReadColorExtra(extras, "SunColor", environment.SunColor);
        if (extras.Has("CamSpeed") && extras.Get("CamSpeed").IsNumber())
        {
            environment.ControlSpeed = static_cast<float>(extras.Get("CamSpeed").GetNumberAsDouble());
        }
        if (extras.Has("ControlSpeed") && extras.Get("ControlSpeed").IsNumber())
        {
            environment.ControlSpeed = static_cast<float>(extras.Get("ControlSpeed").GetNumberAsDouble());
        }
        if (extras.Has("WithSun"))
        {
            environment.HasSun = ReadBoolExtra(extras, "WithSun", environment.HasSun);
        }
        if (extras.Has("HasSun"))
        {
            environment.HasSun = ReadBoolExtra(extras, "HasSun", environment.HasSun);
        }
        if (extras.Has("NoSky"))
        {
            environment.HasSky = false;
        }
        if (extras.Has("HasSky"))
        {
            environment.HasSky = ReadBoolExtra(extras, "HasSky", environment.HasSky);
        }
        if (extras.Has("GammaCorrection"))
        {
            environment.GammaCorrection = ReadBoolExtra(extras, "GammaCorrection", environment.GammaCorrection);
        }

        environment.AtmosphereEnabled =
            ReadBoolExtra(extras, "AtmosphereEnabled", environment.AtmosphereEnabled);
        environment.AerialPerspectiveEnabled =
            ReadBoolExtra(extras, "AerialPerspectiveEnabled", environment.AerialPerspectiveEnabled);
        environment.HeightFogEnabled =
            ReadBoolExtra(extras, "HeightFogEnabled", environment.HeightFogEnabled);

        auto& atmosphere = environment.Atmosphere;
        atmosphere.RayleighScattering =
            ReadColorExtra(extras, "RayleighScattering", atmosphere.RayleighScattering);
        atmosphere.RayleighDensityH =
            ReadFloatExtra(extras, "RayleighDensityH", atmosphere.RayleighDensityH);
        atmosphere.MieScattering =
            ReadColorExtra(extras, "MieScattering", atmosphere.MieScattering);
        atmosphere.MieDensityH =
            ReadFloatExtra(extras, "MieDensityH", atmosphere.MieDensityH);
        atmosphere.MieAbsorption =
            ReadColorExtra(extras, "MieAbsorption", atmosphere.MieAbsorption);
        atmosphere.MiePhaseG =
            ReadFloatExtra(extras, "MiePhaseG", atmosphere.MiePhaseG);
        atmosphere.OzoneAbsorption =
            ReadColorExtra(extras, "OzoneAbsorption", atmosphere.OzoneAbsorption);
        atmosphere.OzoneCenterAltitude =
            ReadFloatExtra(extras, "OzoneCenterAltitude", atmosphere.OzoneCenterAltitude);
        atmosphere.OzoneWidth =
            ReadFloatExtra(extras, "OzoneWidth", atmosphere.OzoneWidth);
        atmosphere.GroundAlbedo =
            ReadColorExtra(extras, "GroundAlbedo", atmosphere.GroundAlbedo);
        atmosphere.BottomRadius =
            ReadFloatExtra(extras, "BottomRadius", atmosphere.BottomRadius);
        atmosphere.TopRadius =
            ReadFloatExtra(extras, "TopRadius", atmosphere.TopRadius);
        atmosphere.WorldUnitsPerKm =
            ReadFloatExtra(extras, "WorldUnitsPerKm", atmosphere.WorldUnitsPerKm);
        atmosphere.WorldOriginAltitude =
            ReadFloatExtra(extras, "WorldOriginAltitude", atmosphere.WorldOriginAltitude);
        atmosphere.AerialPerspectiveMaxDistance =
            ReadFloatExtra(extras, "AerialPerspectiveMaxDistance", atmosphere.AerialPerspectiveMaxDistance);
        atmosphere.SkyLuminanceScale =
            ReadFloatExtra(extras, "SkyLuminanceScale", atmosphere.SkyLuminanceScale);
        atmosphere.FogInscatteringColor =
            ReadColorExtra(extras, "FogInscatteringColor", atmosphere.FogInscatteringColor);
        atmosphere.FogDensity =
            ReadFloatExtra(extras, "FogDensity", atmosphere.FogDensity);
        atmosphere.FogHeightFalloff =
            ReadFloatExtra(extras, "FogHeightFalloff", atmosphere.FogHeightFalloff);
        atmosphere.FogBaseHeight =
            ReadFloatExtra(extras, "FogBaseHeight", atmosphere.FogBaseHeight);
        atmosphere.FogStartDistance =
            ReadFloatExtra(extras, "FogStartDistance", atmosphere.FogStartDistance);
        atmosphere.FogMaxOpacity =
            ReadFloatExtra(extras, "FogMaxOpacity", atmosphere.FogMaxOpacity);
    }
    
    Assets::Camera ParseGltfCamera(const tinygltf::Camera& gltfCamera)
    {
        Assets::Camera camera{};
        camera.name = gltfCamera.name;
        camera.ModelView = glm::mat4(1.0f);
        camera.FieldOfView = 40.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = 1000.0f;

        if (gltfCamera.type == "perspective")
        {
            if (gltfCamera.perspective.yfov > 0.0)
            {
                camera.FieldOfView = static_cast<float>(gltfCamera.perspective.yfov * 180.0 / M_PI);
            }
            if (gltfCamera.perspective.znear > 0.0)
            {
                camera.NearPlane = static_cast<float>(gltfCamera.perspective.znear);
            }
            if (gltfCamera.perspective.zfar > camera.NearPlane)
            {
                camera.FarPlane = static_cast<float>(gltfCamera.perspective.zfar);
            }
        }
        else
        {
            SPDLOG_WARN("glTF camera '{}' uses unsupported type '{}'; using perspective defaults",
                        gltfCamera.name, gltfCamera.type);
        }

        if (gltfCamera.extras.Has("F-Stop") && gltfCamera.extras.Get("F-Stop").IsNumber())
        {
            const double fStop = gltfCamera.extras.Get("F-Stop").GetNumberAsDouble();
            if (fStop > 0.0)
            {
                camera.Aperture = static_cast<float>(0.2 / fStop);
            }
        }
        if (gltfCamera.extras.Has("FocalDistance") && gltfCamera.extras.Get("FocalDistance").IsNumber())
        {
            camera.FocalDistance = static_cast<float>(gltfCamera.extras.Get("FocalDistance").GetNumberAsDouble());
        }

        return camera;
    }

    void ParseGltfNode(std::vector<std::shared_ptr<Assets::Node>>& outNodes, std::map<int, std::shared_ptr<Node> >& nodeMap, Assets::EnvironmentSetting& outCamera, std::vector<Assets::LightObject>& outLights,
        tinygltf::Model& model, int nodeIdx, int modelIdx, int materialOffset, const std::shared_ptr<Node>& parent)
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

        const std::string sceneReferenceAssetPath = ReadSceneReferenceAssetPath(node);
        const bool isSceneReference = !sceneReferenceAssetPath.empty();
        uint32_t meshId = -1;
        if(!isSceneReference && node.mesh != -1)
        {
            meshId = node.mesh + modelIdx;
        }
        else if (!isSceneReference)
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
                if (node.camera >= static_cast<int>(model.cameras.size()))
                {
                    SPDLOG_WARN("glTF node '{}' references invalid camera index {}", node.name, node.camera);
                }
                else
                {
                    Assets::Camera camera = ParseGltfCamera(model.cameras[node.camera]);
                    camera.name = std::to_string(node.camera) + " " + node.name;
                    camera.NodeName_ = node.name;
                    camera.ModelView = modelView;
                    outCamera.cameras.push_back(std::move(camera));
                }
            }
        }

        std::shared_ptr<Node> sceneNode = Node::CreateNode(node.name, translation, rotation, scale, uint32_t(outNodes.size()));
        if (parent)
        {
            sceneNode->SetParent(parent);
        }
        if (node.extras.Has("tag") && node.extras.Get("tag").IsString())
        {
            sceneNode->SetTag(node.extras.Get("tag").Get<std::string>());
        }
        if (node.extras.Has("layer") && node.extras.Get("layer").IsString())
        {
            sceneNode->SetLayer(node.extras.Get("layer").Get<std::string>());
        }
        if (node.extras.Has("gkEnvironment") && node.extras.Get("gkEnvironment").IsObject())
        {
            auto environment = std::make_shared<Runtime::EnvironmentComponent>();
            ReadEnvironmentExtras(node.extras.Get("gkEnvironment"), *environment);
            sceneNode->AddComponent(environment);
        }

        if (isSceneReference)
        {
            auto sceneReference = std::make_shared<Runtime::SceneReferenceComponent>();
            sceneReference->SetAssetPath(sceneReferenceAssetPath);
            sceneNode->AddComponent(sceneReference);
            outNodes.push_back(sceneNode);
            nodeMap[nodeIdx] = sceneNode;

            if (node.mesh != -1 || node.camera >= 0 || node.skin != -1 || !node.children.empty())
            {
                SPDLOG_WARN("Scene reference node '{}' ignores local mesh/camera/skin/children", node.name);
            }
            return;
        }

        if (meshId != -1)
        {
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(meshId);
            renderComp->SetVisible(true);
            std::array<uint32_t, 16> materialIdx {};
            for (int i = 0; i < model.meshes[node.mesh].primitives.size(); i++)
            {
                assert( i < 16 );
                auto& primitive = model.meshes[node.mesh].primitives[i];
                materialIdx[i] = (max(0, primitive.material + materialOffset));
            }
            renderComp->SetMaterials(materialIdx);

            auto readNodeBoolExtra = [&node](const char* key, bool fallback)
            {
                if (!node.extras.Has(key))
                {
                    return fallback;
                }
                const tinygltf::Value& value = node.extras.Get(key);
                if (value.IsBool())
                {
                    return value.Get<bool>();
                }
                if (value.IsNumber())
                {
                    return value.GetNumberAsInt() != 0;
                }
                return fallback;
            };

            renderComp->SetVisible(readNodeBoolExtra("visible", renderComp->GetVisible()));
            renderComp->SetCastShadows(readNodeBoolExtra("castShadows", renderComp->GetCastShadows()));
            renderComp->SetReceiveGI(readNodeBoolExtra("receiveGI", renderComp->GetReceiveGI()));
            if (node.extras.Has("layerMask") && node.extras.Get("layerMask").IsNumber())
            {
                renderComp->SetLayerMask(static_cast<uint32_t>(node.extras.Get("layerMask").GetNumberAsDouble()));
            }

            sceneNode->AddComponent(renderComp);
        }

        if (node.skin != -1)
        {
            if (auto render = sceneNode->GetComponent<Runtime::RenderComponent>()) 
            {
                render->SetSkinIndex(node.skin);
            }
        }
        else
        {
            if (auto render = sceneNode->GetComponent<Runtime::RenderComponent>()) 
            {
                render->SetSkinIndex(0xFFFFFFFF);
            }
        }

        outNodes.push_back(sceneNode);

        nodeMap[nodeIdx] = sceneNode;

        if( node.extras.Has("arealight") )
        {
            bool foundAreaLight = false;
            if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
            {
                for (const tinygltf::Primitive& primitive : model.meshes[node.mesh].primitives)
                {
                    if (!IsPureEmissivePrimitive(model, primitive))
                    {
                        continue;
                    }
                    LightObject light{};
                    if (TryBuildAreaLight(model, primitive, glm::mat4(1.0f), materialOffset, light))
                    {
                        auto* lightComponent = sceneNode->GetComponent<Runtime::LightComponent>();
                        if (!lightComponent)
                        {
                            auto newLightComponent = std::make_shared<Runtime::LightComponent>();
                            lightComponent = newLightComponent.get();
                            sceneNode->AddComponent(std::move(newLightComponent));
                        }
                        lightComponent->AddLight(light);
                        foundAreaLight = true;
                    }
                    else
                    {
                        SPDLOG_WARN("Area-light node '{}' has an emissive primitive that is not a two-triangle quad",
                                    node.name);
                    }
                }
            }
            if (!foundAreaLight)
            {
                SPDLOG_WARN("Area-light node '{}' does not contain a supported pure-emissive quad", node.name);
            }
        }
        
        // for each child node
        for (int child : node.children)
        {
            ParseGltfNode(outNodes, nodeMap, outCamera, outLights, model, child, modelIdx, materialOffset, sceneNode);
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
            // Route tinygltf file IO through FPackageFileSystem so .gltf + .bin + sidecar textures
            // can be loaded from optional.pak when missing on disk.
            tinygltf::FsCallbacks fs{};
            fs.FileExists = [](const std::string& abs, void*) -> bool {
                std::vector<uint8_t> tmp;
                return Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(abs, tmp);
            };
            fs.ExpandFilePath = [](const std::string& path, void*) -> std::string {
                return path;
            };
            fs.ReadWholeFile = [](std::vector<unsigned char>* out, std::string* err,
                                  const std::string& path, void*) -> bool {
                std::vector<uint8_t> data;
                if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(path, data))
                {
                    if (err) { *err = "FPackageFileSystem failed to load: " + path; }
                    return false;
                }
                out->assign(data.begin(), data.end());
                return true;
            };
            fs.WriteWholeFile = [](std::string* err, const std::string&,
                                   const std::vector<unsigned char>&, void*) -> bool {
                if (err) { *err = "WriteWholeFile not supported via FPackageFileSystem"; }
                return false;
            };
            fs.GetFileSizeInBytes = [](size_t* sizeOut, std::string* err,
                                       const std::string& path, void*) -> bool {
                std::vector<uint8_t> data;
                if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(path, data))
                {
                    if (err) { *err = "FPackageFileSystem failed to stat: " + path; }
                    return false;
                }
                if (sizeOut) { *sizeOut = data.size(); }
                return true;
            };
            gltfLoader.SetFsCallbacks(fs);

            if(!gltfLoader.LoadASCIIFromFile(&model, &err, &warn, filename) )
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
                    const std::filesystem::path fileUriPath = filepath.parent_path() / std::filesystem::path(image.uri);
                    uint32_t texIdx = GlobalTexturePool::LoadTexture(Utilities::FileHelper::NormalizePathString(fileUriPath), srgb);
                    textureIdMap[imageIdx] = texIdx;
                }
                else
                {
                    const tinygltf::BufferView& imageView = model.bufferViews[image.bufferView];
                    uint32_t texIdx = GlobalTexturePool::LoadTexture(
                        currSceneName + texname, model.images[imageIdx].mimeType,
                        model.buffers[imageView.buffer].data.data() + imageView.byteOffset,
                        imageView.byteLength, srgb);
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
            skeleton.RootJointIndex = 0;
            
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

            m.EmissiveTextureId = lambdaGetTexture(mat.emissiveTexture.index);

            if (mat.occlusionTexture.index != -1 && mat.occlusionTexture.index != mat.pbrMetallicRoughness.metallicRoughnessTexture.index)
            {
                //SPDLOG_WARN("Material '{}': Separate Occlusion texture not supported. Pack it into the R channel of Metallic-Roughness texture.", mat.name);
            }
            if (mat.alphaMode != "OPAQUE")
            {
                 //SPDLOG_WARN("Material '{}': Alpha mode '{}' not fully supported (assumed OPAQUE).", mat.name, mat.alphaMode);
            }
            if (mat.doubleSided)
            {
                 //SPDLOG_WARN("Material '{}': Double sided not supported.", mat.name);
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

            float emissiveStrength = 1.0f;
            auto emissiveExtension = mat.extensions.find("KHR_materials_emissive_strength");
            if (emissiveExtension != mat.extensions.end())
            {
                emissiveStrength = static_cast<float>(emissiveExtension->second.Get("emissiveStrength").GetNumberAsDouble()) * 50.f;
            }

            bool isPureEmissiveMaterial = (glm::length(emissiveColor) > 0.001f) && (mat.emissiveTexture.index == -1);
            if (isPureEmissiveMaterial)
            {
                m.MaterialModel = Material::Enum::DiffuseLight;
                m.Diffuse = glm::vec4(emissiveColor * emissiveStrength, 1.0f);
            }

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

            if (mat.emissiveTexture.index != -1)
            {
                m.EmissiveTextureId = lambdaGetTexture(mat.emissiveTexture.index);
            }

            FMaterial fmat;
            fmat.gpuMaterial_ = m;
            fmat.name_ = mat.name;

            materials.push_back(fmat);
        }

        // export whole scene into a big buffer, with vertices, indices, materials
        for (tinygltf::Mesh& mesh : model.meshes)
        {
            bool hasTangent = false;
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<glm::vec4> weights;
            std::vector<glm::uvec4> joints;

            uint32_t vertexOffset = 0;
            uint32_t sectionIdx = 0;
            for (tinygltf::Primitive& primitive : mesh.primitives)
            {
                if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                {
                    continue;
                }

                if (primitive.indices != -1)
                {
                    const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                    if (indexAccessor.count == 0)
                    {
                        continue;
                    }
                }
               
                int posIdx = -1;
                if (primitive.attributes.find("POSITION") != primitive.attributes.end()) posIdx = primitive.attributes["POSITION"];
                int normIdx = -1;
                if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) normIdx = primitive.attributes["NORMAL"];
                int uvIdx = -1;
                if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) uvIdx = primitive.attributes["TEXCOORD_0"];
                int tanIdx = -1;
                if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) tanIdx = primitive.attributes["TANGENT"];

                if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end())
                {
                     // SPDLOG_WARN("Mesh '{}': TEXCOORD_1 found but ignored.", mesh.name);
                }
                
                int jointsIdx = -1;
                if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) jointsIdx = primitive.attributes["JOINTS_0"];
                int weightsIdx = -1;
                if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) weightsIdx = primitive.attributes["WEIGHTS_0"];

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
                
                if (primitive.indices != -1)
                {
                    const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
                    int strideIndex = indexAccessor.ByteStride(indexView);

                    for (size_t i = 0; i < indexAccessor.count; ++i)
                    {
                        if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT )
                        {
                            uint16* data = reinterpret_cast<uint16*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                            indices.push_back(*data + vertexOffset);
                        }
                        else if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT )
                        {
                            uint32* data = reinterpret_cast<uint32*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                            indices.push_back(*data + vertexOffset);
                        }
                        else if( indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE )
                        {
                            uint8_t* data = reinterpret_cast<uint8_t*>(&model.buffers[indexView.buffer].data[indexView.byteOffset + indexAccessor.byteOffset + i * strideIndex]);
                            indices.push_back(*data + vertexOffset);
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
                        indices.push_back(static_cast<uint32_t>(i) + vertexOffset);
                    }
                }

                vertexOffset += static_cast<uint32_t>(positionAccessor.count);
            }
            
            models.push_back(Assets::Model(mesh.name, std::move(vertices), std::move(indices), !hasTangent));
            if (!weights.empty())
            {
                models.back().CPUWeights() = std::move(weights);
                models.back().CPUJoints() = std::move(joints);
            }
        }



        auto& root = model.scenes[0];
        ReadEnvironmentExtras(root.extras, cameraInit);

        // gltf scenes contain the rootnodes
        //std::shared_ptr<Node> sceneNode = Node::CreateNode("Root", glm::vec3(0,0,0), glm::quat(1,0,0,0), glm::vec3(10,10,10), -1, nodes.size(), false);
        //nodes.push_back(sceneNode);
        
        std::map<int, std::shared_ptr<Node> > nodeMap;
        for (int nodeIdx : model.scenes[0].nodes)
        {
            ParseGltfNode(nodes, nodeMap, cameraInit, lights, model, nodeIdx, modelIdx, materialOffset, nullptr);
            // if (nodeMap.find(nodeIdx) != nodeMap.end())
            // {
            //     nodeMap[nodeIdx]->SetParent(sceneNode);
            // }
        }

        // default auto camera
        Camera defaultCam = Assets::AutoFocusCamera(cameraInit, nodes, models);

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

        return true;
    }

    bool FSceneLoader::LoadAnimationTracks(const std::string& filename,
                                           std::vector<AnimationTrack>& outTracks)
    {
        EnvironmentSetting dummyCam;
        std::vector<std::shared_ptr<Node>> dummyNodes;
        std::vector<Model> dummyModels;
        std::vector<FMaterial> dummyMats;
        std::vector<LightObject> dummyLights;
        std::vector<Skeleton> dummySkels;
        return LoadGLTFScene(filename, dummyCam, dummyNodes, dummyModels,
                             dummyMats, dummyLights, outTracks, dummySkels);
    }
}
