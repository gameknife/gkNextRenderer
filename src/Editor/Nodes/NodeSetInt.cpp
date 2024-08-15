#include "NodeSetInt.hpp"

namespace Nodes
{

    NodeSetInt::NodeSetInt(const std::string name, int initValue)
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

        setStyle(ImFlow::NodeStyle::green());
        addOUT<int>("OUT")
            ->behaviour([this]()
                        { return value; });
    }

    void NodeSetInt::draw()
    {
        ImGui::SetNextItemWidth(100.f);
        ImGui::InputInt("##Val", &value);
    }
}