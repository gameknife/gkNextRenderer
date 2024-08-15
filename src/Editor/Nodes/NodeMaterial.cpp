#include "NodeMaterial.hpp"

#include <glm/vec3.hpp>

namespace Nodes
{
    NodeMaterial::NodeMaterial()
    {
        setTitle("Material Node");
        setStyle(ImFlow::NodeStyle::brown());
        addIN<glm::vec3>("Albedo", glm::vec3(1,1,1), ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::white());
        addIN<float>("Roughness", 0.0, ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::white());
        addIN<float>("Metalness", 0.0, ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::white());
        addIN<float>("Emissive", 0.0, ImFlow::ConnectionFilter::SameType(), ImFlow::PinStyle::white());
    }

    void NodeMaterial::draw()
    {
        glm::vec3 inVal1 = getInVal<glm::vec3>("Albedo");
        float inVal2 = getInVal<float>("Roughness");
        float inVal3 = getInVal<float>("Metalness");
        float inVal4 = getInVal<float>("Emissive");
        
        ImGui::ColorEdit3("Albedo", (float*)&inVal1, ImGuiColorEditFlags_NoInputs);
        ImGui::Text("Roughness %.2f", inVal2);
        ImGui::Text("Metalness %.2f", inVal3);
        ImGui::Text("Emissive %.2f", inVal4);
    }
}
