#include "NodeClassification.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Editor
{
    namespace
    {
        constexpr ImVec4 FromU8(int r, int g, int b)
        {
            return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                          static_cast<float>(b) / 255.0f, 1.0f);
        }

        constexpr FNodeVisual kVisuals[] = {
            {ENodeKind::Reference, ICON_FA_LINK, IM_COL32(56, 189, 248, 255), FromU8(56, 189, 248),
             "Reference", "Scene Reference"},
            {ENodeKind::Camera, ICON_FA_VIDEO, IM_COL32(96, 165, 250, 255), FromU8(96, 165, 250),
             "Camera", "Camera Actor"},
            {ENodeKind::Light, ICON_FA_LIGHTBULB, IM_COL32(251, 191, 36, 255), FromU8(251, 191, 36),
             "Light", "Light Actor"},
            {ENodeKind::Mesh, ICON_FA_CUBE, IM_COL32(251, 146, 60, 255), FromU8(251, 146, 60),
             "Mesh", "Mesh Actor"},
            {ENodeKind::Group, ICON_FA_FOLDER, IM_COL32(148, 163, 184, 255), FromU8(148, 163, 184),
             "Group", "Scene Node with children"},
            {ENodeKind::Empty, ICON_FA_CIRCLE_NOTCH, IM_COL32(100, 116, 139, 255), FromU8(100, 116, 139),
             "Empty", "Scene Node"},
        };

        const FNodeVisual& VisualFor(ENodeKind kind)
        {
            for (const FNodeVisual& visual : kVisuals)
            {
                if (visual.kind == kind)
                {
                    return visual;
                }
            }
            return kVisuals[IM_ARRAYSIZE(kVisuals) - 1];
        }

        bool NameContains(const Assets::Node& node, std::string_view needle)
        {
            std::string lower = node.GetName();
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower.find(needle) != std::string::npos;
        }

        ENodeKind KindOf(const Assets::Node& node)
        {
            if (node.GetComponent<Runtime::SceneReferenceComponent>() != nullptr)
            {
                return ENodeKind::Reference;
            }
            if (NameContains(node, "camera"))
            {
                return ENodeKind::Camera;
            }
            if (NameContains(node, "light") || NameContains(node, "sun"))
            {
                return ENodeKind::Light;
            }
            if (auto render = node.GetComponent<Runtime::RenderComponent>();
                render != nullptr && render->GetModelId() >= 0)
            {
                return ENodeKind::Mesh;
            }
            return node.Children().empty() ? ENodeKind::Empty : ENodeKind::Group;
        }
    } // namespace

    const FNodeVisual& ClassifyNode(const Assets::Node& node)
    {
        return VisualFor(KindOf(node));
    }

    bool NodeMatchesKind(const Assets::Node& node, ENodeKind filter)
    {
        if (filter == ENodeKind::All)
        {
            return true;
        }
        return KindOf(node) == filter;
    }

    bool SubtreeMatchesKind(const Assets::Node& node, ENodeKind filter)
    {
        if (filter == ENodeKind::All)
        {
            return true;
        }
        if (NodeMatchesKind(node, filter))
        {
            return true;
        }
        for (const auto& child : node.Children())
        {
            if (child->IsSceneReferenceInternal())
            {
                continue;
            }
            if (SubtreeMatchesKind(*child, filter))
            {
                return true;
            }
        }
        return false;
    }
} // namespace Editor
