#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace Assets
{
    class Node;
}

namespace Editor
{
    // How the editor buckets a scene node for display and filtering. The scene has no authoritative
    // node type, so this is a heuristic over components and names -- kept in one place so the
    // Outliner icon, the Outliner type filter and the Properties header chip can never disagree.
    enum class ENodeKind
    {
        All = 0, // Filter-only: matches every node.
        Mesh = 1,
        Light = 2,
        Camera = 3,
        Reference,
        Group,
        Empty,
    };

    struct FNodeVisual
    {
        ENodeKind kind = ENodeKind::Empty;
        const char* icon = nullptr;
        ImU32 color = 0;
        ImVec4 tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        const char* label = "";
        const char* description = "";
    };

    const FNodeVisual& ClassifyNode(const Assets::Node& node);

    // True when the node itself matches the filter. ENodeKind::All matches everything.
    bool NodeMatchesKind(const Assets::Node& node, ENodeKind filter);

    // True when the node or any of its (non-internal) descendants matches the filter, so a parent
    // stays visible when only a child matches.
    bool SubtreeMatchesKind(const Assets::Node& node, ENodeKind filter);
} // namespace Editor
