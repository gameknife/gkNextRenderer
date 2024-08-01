#pragma once

#include "Material.hpp"
#include "Procedural.hpp"
#include "Vertex.hpp"
#include "Texture.hpp"
#include "UniformBuffer.hpp"

#include <memory>
#include <string>
#include <vector>

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

        std::vector<Camera> cameras;
    };

    struct alignas(16) NodeProxy final
	{
        glm::mat4 transform;
    };

    class Node final
    {
    public:
        static Node CreateNode(std::string name, glm::mat4 transform, int id, bool procedural);
        Node& operator =(const Node&) = delete;
        Node& operator =(Node&&) = delete;

        Node() = default;
        Node(const Node&) = default;
        Node(Node&&) = default;
        ~Node() = default;

        void Transform(const glm::mat4& transform) { transform_ = transform; }
        const glm::mat4& WorldTransform() const { return transform_; }
        int GetModel() const { return modelId_; }
        bool IsProcedural() const { return procedural_; }
        const std::string& GetName() const {return name_; }

    private:
        Node(std::string name, glm::mat4 transform, int id, bool procedural);

        std::string name_;
        glm::mat4 transform_;
        int modelId_;
        bool procedural_;
    };
    
    class Model final
    {
    public:
        static void FlattenVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

        static void AutoFocusCamera(Assets::CameraInitialSate& cameraInit, std::vector<Model>& models);

        static int LoadObjModel(const std::string& filename, std::vector<Node>& nodes, std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights, bool autoNode = true);

        static int CreateCornellBox(const float scale,
                                     std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights);
        static int CreateLightQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                     const glm::vec3& dir, const glm::vec3& lightColor,
                                     std::vector<Model>& models,
                                     std::vector<Material>& materials,
                                     std::vector<LightObject>& lights);
        static void LoadGLTFScene(const std::string& filename, Assets::CameraInitialSate& cameraInit, std::vector<class Node>& nodes,
                                  std::vector<Assets::Model>& models, std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);

        // basic geometry
        static Model CreateBox(const glm::vec3& p0, const glm::vec3& p1, int materialIdx);
        static Model CreateSphere(const glm::vec3& center, float radius, int materialIdx, bool isProcedural);

        Model& operator =(const Model&) = delete;
        Model& operator =(Model&&) = delete;

        Model() = default;
        Model(const Model&) = default;
        Model(Model&&) = default;
        ~Model() = default;

        const std::vector<Vertex>& Vertices() const { return vertices_; }
        const std::vector<uint32_t>& Indices() const { return indices_; }
        const std::vector<uint32_t>& Materials() const { return materialIdx_; }

        const class Procedural* Procedural() const { return procedural_.get(); }

        uint32_t NumberOfVertices() const { return static_cast<uint32_t>(vertices_.size()); }
        uint32_t NumberOfIndices() const { return static_cast<uint32_t>(indices_.size()); }

        glm::vec3 GetLocalAABBMin() {return local_aabb_min;}
        glm::vec3 GetLocalAABBMax() {return local_aabb_max;}

    private:
        Model(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, std::vector<uint32_t>&& materials, const class Procedural* procedural);

        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;
        std::shared_ptr<const class Procedural> procedural_;
        std::vector<uint32_t> materialIdx_;

        glm::vec3 local_aabb_min;
        glm::vec3 local_aabb_max;
    };
}
