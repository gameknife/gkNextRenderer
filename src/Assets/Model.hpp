#pragma once

#include "Material.hpp"
#include "Procedural.hpp"
#include "Vertex.hpp"
#include "Texture.hpp"
#include "UniformBuffer.hpp"
#include "Runtime/NextPhysics.h"

#include <memory>
#include <set>
#include <string>
#include <vector>
#include <glm/detail/type_quat.hpp>

struct FNextPhysicsBody;

namespace Assets
{
    struct Camera final
    {
        std::string name;
        glm::mat4 ModelView;
        float FieldOfView;
        float Aperture;
        float FocalDistance;
    };

    struct EnvironmentSetting
    {
        EnvironmentSetting()
        {
            Reset();
        }
        
        void Reset()
        {
            ControlSpeed = 5.0f;
            GammaCorrection = true;
            HasSky = true;
            HasSun = false;
            SkyIdx = 0;
            SunIntensity = 500.f;
            SkyIntensity = 100.0f;
            SkyRotation = 0;
            SunRotation = 0.5f;   
        }
        
        float ControlSpeed;
        bool GammaCorrection;
        bool HasSky;
        bool HasSun;
        int32_t SkyIdx;
        float SunRotation;
        float SkyRotation;
        
        float SkyIntensity = 100.0f;
        float SunIntensity = 500.0f;

        std::vector<Camera> cameras;
    };

    // node tree to represent the scene
    // but in rendering, it will flatten to renderproxys
    class Node : public std::enable_shared_from_this<Node>
    {
    public:
        static std::shared_ptr<Node> CreateNode(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t modelId, uint32_t instanceId, bool replace);
        Node(std::string name,  glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t id, uint32_t instanceId, bool replace);
        
        void SetTranslation( glm::vec3 translation );
        void SetRotation( glm::quat rotation );
        void SetScale( glm::vec3 scale );

        glm::vec3& Translation() const { return translation_; }
        glm::quat& Rotation() const { return rotation_; }
        glm::vec3& Scale() const { return scaling_; }

        void RecalcLocalTransform();
        void RecalcTransform(bool full = true);
        const glm::mat4& WorldTransform() const { return transform_; }
        uint32_t GetModel() const { return modelId_; }
        const std::string& GetName() const {return name_; }

        void SetVisible(bool visible) { visible_ = visible; }
        bool IsVisible() const { return visible_; }

        uint32_t GetInstanceId() const { return instanceId_; }
        bool TickVelocity(glm::mat4& combinedTS);

        void SetParent(std::shared_ptr<Node> parent);
        Node* GetParent() { return parent_.get(); }

        void AddChild(std::shared_ptr<Node> child);
        void RemoveChild(std::shared_ptr<Node> child);

        const std::set< std::shared_ptr<Node> >& Children() const { return children_; }

        void SetMaterial(const std::vector<uint32_t>& materials);
        std::vector<uint32_t>& Materials() { return materialIdx_; }
        NodeProxy GetNodeProxy() const;

        void BindPhysicsBody(JPH::BodyID bodyId) { physicsBodyTemp_ = bodyId; }
        
    private:
        std::string name_;

        mutable glm::vec3 translation_;
        mutable glm::quat rotation_;
        mutable glm::vec3 scaling_;

        glm::mat4 localTransform_;
        glm::mat4 transform_;
        glm::mat4 prevTransform_;
        uint32_t modelId_;
        uint32_t instanceId_;
        bool visible_;

        std::shared_ptr<Node> parent_;
        std::set< std::shared_ptr<Node> > children_;
        std::vector<uint32_t> materialIdx_;
        JPH::BodyID physicsBodyTemp_;
    };

    template <typename T>
    struct AnimationKey
    {
        float Time;
        T Value;
    };

    template <typename T>
    struct AnimationChannel
    {
        std::vector<AnimationKey<T>> Keys;
        T Sample(float time);
    };
    
    struct AnimationTrack
    {
        bool Playing() const { return Playing_; }
        void Play() { Playing_ = true; }
        void Stop() { Playing_ = false; }
        
        void Sample(float time, glm::vec3& translation, glm::quat& rotation, glm::vec3& scaling);
        
        std::string NodeName_;
        
        AnimationChannel<glm::vec3> TranslationChannel;
        AnimationChannel<glm::quat> RotationChannel;
        AnimationChannel<glm::vec3> ScaleChannel;
        
        float Time_;
        float Duration_;

        bool Playing_{};
    };
    
    class Model final
    {
    public:
        static void FlattenVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

        static Camera AutoFocusCamera(Assets::EnvironmentSetting& cameraInit, std::vector<Model>& models);
        
        static uint32_t CreateCornellBox(const float scale,
                                     std::vector<Model>& models,
                                     std::vector<FMaterial>& materials,
                                     std::vector<LightObject>& lights);
        static uint32_t CreateLightQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                     const glm::vec3& dir, const glm::vec3& lightColor,
                                     std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights);
        static bool LoadGLTFScene(const std::string& filename, Assets::EnvironmentSetting& cameraInit, std::vector< std::shared_ptr<Assets::Node> >& nodes,
                                  std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks);

        // basic geometry
        static Model CreateBox(const glm::vec3& p0, const glm::vec3& p1);
        static Model CreateSphere(const glm::vec3& center, float radius);

        Model& operator =(const Model&) = delete;
        Model& operator =(Model&&) = delete;

        Model() = default;
        Model(const Model&) = default;
        Model(Model&&) = default;
        ~Model() = default;

        std::vector<Vertex>& CPUVertices() { return vertices_; }
        const std::vector<uint32_t>& CPUIndices() const { return indices_; }
        
        glm::vec3 GetLocalAABBMin() {return local_aabb_min;}
        glm::vec3 GetLocalAABBMax() {return local_aabb_max;}

        uint32_t NumberOfVertices() const { return verticeCount; }
        uint32_t NumberOfIndices() const { return indiceCount; }

        void FreeMemory();

    private:
        Model(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, bool needGenTSpace = true);

        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;
        
        std::vector<AnimationTrack> AnimationTracks_;
        
        glm::vec3 local_aabb_min;
        glm::vec3 local_aabb_max;

        uint32_t verticeCount;
        uint32_t indiceCount;
    };
}
