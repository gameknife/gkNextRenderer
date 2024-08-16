#include "NodeSetFloat.hpp"

namespace Nodes
{
    NodeSetFloat::NodeSetFloat(const std::string name, float initValue)
    {
        if (name == "")
        {
            setTitle("Set float");
        }
        else
        {
            setTitle(name);
        }

        value1 = initValue;
        value2 = initValue;

        setStyle(ImFlow::NodeStyle::green());
        addOUT<float>("Out", ImFlow::PinStyle::white())
            ->behaviour([this]()
                        { return value1; });
    }

    void NodeSetFloat::draw()
    {
        ImGui::SetNextItemWidth(100.f);
        ImGui::InputFloat("##Val1", &value1, 0.1f, 1.0f, "%.1f");
    }

    NodeSetColor::NodeSetColor(const std::string name, glm::vec3 initValue)
    {
        if (name == "")
        {
            setTitle("Set float");
        }
        else
        {
            setTitle(name);
        }

        value = initValue;

        setStyle(ImFlow::NodeStyle::red());
        addOUT<glm::vec3>("Out", ImFlow::PinStyle::white())
            ->behaviour([this]()
                        { return value; });
    }

    void NodeSetColor::draw()
    {
        ImGui::SetNextItemWidth(100.f);
        ImGui::ColorEdit3("Color", (float*)&value, ImGuiColorEditFlags_NoInputs);
    }
}
