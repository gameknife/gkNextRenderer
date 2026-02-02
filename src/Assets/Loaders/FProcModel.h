#pragma once
#include "Assets/Core/Model.hpp"

namespace Assets
{
    struct FMaterial;
    
    class FProcModel
    {
    public:
        // basic geometry
        static Model CreateBox(const glm::vec3& p0, const glm::vec3& p1);
        static Model CreateSphere(const glm::vec3& center, float radius);
        static uint32_t CreateCornellBox(const float scale,std::vector<Model>& models,std::vector<FMaterial>& materials,std::vector<LightObject>& lights);
    };

}
