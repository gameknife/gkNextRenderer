#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"

namespace Assets
{
    class FProcModel
    {
    public:
        // basic geometry
        static Model CreateBox(const glm::vec3& p0, const glm::vec3& p1);
        static Model CreateSphere(const glm::vec3& center, float radius);
        static Model CreateFromBuffers(const std::string& name,
                                       std::vector<Vertex>&& vertices,
                                       std::vector<uint32_t>&& indices,
                                       bool needGenTSpace = true);
        static Model CreateExtrudedConvexPolygon(const std::string& name,
                                                 const std::vector<glm::vec2>& polygonXZ,
                                                 float minY,
                                                 float maxY);
        static uint32_t CreateCornellBox(const float scale,std::vector<Model>& models,std::vector<FMaterial>& materials,std::vector<LightObject>& lights);

        // Emissive quad. origin = one corner; right/up = two edge vectors.
        // Quad corners are origin, origin+right, origin+right+up, origin+up (CCW from normal side).
        // The normal direction is normalize(cross(right, up)); a LightObject is appended to `lights`.
        static Model CreateAreaLight(const std::string& name,
                                     const glm::vec3& origin,
                                     const glm::vec3& right,
                                     const glm::vec3& up,
                                     uint32_t lightMatIdx,
                                     std::vector<LightObject>& lights);
    };

}
