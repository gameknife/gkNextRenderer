#pragma once

#include "Material.hpp"
#include "Procedural.hpp"
#include "Vertex.hpp"
#include "Texture.hpp"
#include "UniformBuffer.hpp"

#include <memory>
#include <string>
#include <vector>
#include <glm/detail/type_quat.hpp>

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

    struct CameraInitialSate
    {
        glm::mat4 ModelView;
        float FieldOfView;
        float Aperture;
        float FocusDistance;
        float ControlSpeed;
        bool GammaCorrection;
        bool HasSky;
        bool HasSun;
        uint32_t SkyIdx, CameraIdx;
        float SunRotation;
        float SkyRotation;
        
        float SkyIntensity = 100.0f;
        float SunIntensity = 500.0f;

        std::vector<Camera> cameras;
    };

    struct alignas(16) NodeProxy final
	{
        glm::mat4 transform;
    };
    
    struct alignas(16) NodeSimpleProxy final
    {
        uint32_t instanceId;
        uint32_t modelId;
        uint32_t matId;
        uint32_t reserved2;
        glm::vec4 velocityWS;
    };

    class Node final
    {
    public:
        static Node CreateNode(std::string name, glm::mat4 transform, uint32_t modelId, uint32_t instanceId, bool replace);
        
        //Node& operator =(const Node&) = delete;
        //Node& operator =(Node&&) = delete;

        // Node() = default;
        // Node(const Node&) = default;
        // Node(Node&&) = default;
        // ~Node() = default;

        //void Transform(const glm::mat4& transform) { transform_ = transform; }
        const glm::mat4& WorldTransform() const { return transform_; }
        uint32_t GetModel() const { return modelId_; }
        const std::string& GetName() const {return name_; }

        void SetVisible(bool visible) { visible_ = visible; }
        bool IsVisible() const { return visible_; }

        uint32_t GetInstanceId() const { return instanceId_; }

        glm::vec3 TickVelocity();

    private:
        Node(std::string name, glm::mat4 transform, uint32_t id, uint32_t instanceId, bool replace);

        std::string name_;
        glm::mat4 transform_;
        glm::mat4 prevTransform_;
        uint32_t modelId_;
        uint32_t instanceId_;
        bool visible_;
    };

    struct AnimationKey
    {
        float Time;
        glm::vec3 Translation;
        glm::vec3 Scale;
        glm::quat Rotation;
    };
    
    struct AnimationTrack
    {
        std::string NodeName_;
        std::vector<AnimationKey> KeyFrames_;
    };
    
    class Model final
    {
    public:
        static void FlattenVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

        static void AutoFocusCamera(Assets::CameraInitialSate& cameraInit, std::vector<Model>& models);

        static int LoadObjModel(const std::string& filename, std::vector<Node>& nodes, std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights, bool autoNode = true);

        static uint32_t CreateCornellBox(const float scale,
                                     std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights);
        static uint32_t CreateLightQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                     const glm::vec3& dir, const glm::vec3& lightColor,
                                     std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights);
        static void LoadGLTFScene(const std::string& filename, Assets::CameraInitialSate& cameraInit, std::vector<class Node>& nodes,
                                  std::vector<Assets::Model>& models, std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks);

        // basic geometry
        static Model CreateBox(const glm::vec3& p0, const glm::vec3& p1, uint32_t materialIdx);
        static Model CreateSphere(const glm::vec3& center, float radius, uint32_t materialIdx, bool isProcedural);

        Model& operator =(const Model&) = delete;
        Model& operator =(Model&&) = delete;

        Model() = default;
        Model(const Model&) = default;
        Model(Model&&) = default;
        ~Model() = default;

        std::vector<Vertex>& Vertices() { return vertices_; }
        const std::vector<uint32_t>& Indices() const { return indices_; }
        const std::vector<uint32_t>& Materials() const { return materialIdx_; }

        const class Procedural* Procedural() const { return procedural_.get(); }

        uint32_t NumberOfVertices() const { return static_cast<uint32_t>(vertices_.size()); }
        uint32_t NumberOfIndices() const { return static_cast<uint32_t>(indices_.size()); }

        glm::vec3 GetLocalAABBMin() {return local_aabb_min;}
        glm::vec3 GetLocalAABBMax() {return local_aabb_max;}

    private:
        Model(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, std::vector<uint32_t>&& materials, bool needGenTSpace = true);

        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;
        std::vector<uint32_t> materialIdx_;
        std::shared_ptr<const class Procedural> procedural_;

        std::vector<AnimationTrack> AnimationTracks_;
        
        glm::vec3 local_aabb_min;
        glm::vec3 local_aabb_max;
    };
}
