#include "NodeSetInt.hpp"

#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan_core.h>

#include "Runtime/UserInterface.hpp"
#include "Assets/Texture.hpp"
#include "Assets/TextureImage.hpp"
#include "Vulkan/ImageView.hpp"

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
        addOUT<int>("Out")
            ->behaviour([this]()
                        { return value; });
    }

    void NodeSetInt::draw()
    {
        ImGui::SetNextItemWidth(100.f);
        ImGui::InputInt("##Val", &value);
    }

    NodeSetTexture::NodeSetTexture(const std::string name, int initTextureId)
    {
        if (name == "")
        {
            setTitle("Set float");
        }
        else
        {
            setTitle(name);
        }

        textureId = initTextureId;
        
        //

        setStyle(ImFlow::NodeStyle::green());
        addOUT<int>("Out")
            ->behaviour([this]()
                        { return textureId; });
    }

    NodeSetTexture::~NodeSetTexture()
    {
    }

    void NodeSetTexture::draw()
    {
        //ImGui::SetNextItemWidth(100.f);
        // if(textureId != -1 && GUserInterface)
        // {
        //     ImGui::Image( GUserInterface->RequestImTextureId(textureId), ImVec2(128, 128));
        // }
    }
}
