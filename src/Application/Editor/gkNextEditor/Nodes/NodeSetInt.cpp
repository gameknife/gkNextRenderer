#include "NodeSetInt.hpp"

#include "EditorContext.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"

namespace Nodes
{

    NodeSetInt::NodeSetInt(const std::string name, int initValue)
    {
        if (name == "")
        {
            setTitle("Set int");
        }
        else
        {
            setTitle(name);
        }

        value = initValue;

        setStyle(ImFlow::NodeStyle::green());
        addOUT<int>("Out")->behaviour([this]() { return value; });
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
            setTitle("Set texture");
        }
        else
        {
            setTitle(name);
        }

        textureId = initTextureId;

        //

        setStyle(ImFlow::NodeStyle::green());
        addOUT<int>("Out")->behaviour([this]() { return textureId; });
    }

    NodeSetTexture::~NodeSetTexture() {}

    void NodeSetTexture::draw()
    {
        // ImGui::SetNextItemWidth(100.f);
        if (textureId == -1)
        {
            return;
        }

        EditorContext* ctx = static_cast<EditorContext*>(ImGui::GetIO().UserData);
        if (ctx == nullptr)
        {
            return;
        }

        ImTextureID tex = ctx->ui.RequestImTextureId(static_cast<uint32_t>(textureId));
        if (tex != 0)
        {
            ImGui::Image(tex, ImVec2(128, 128));
        }
    }
} // namespace Nodes
