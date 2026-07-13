#pragma once

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Runtime::Command::SelectionUtils
{
    constexpr uint32_t InvalidNodeId = static_cast<uint32_t>(-1);

    inline std::vector<uint32_t> BuildUniqueValidSelection(Assets::Scene& scene, const std::vector<uint32_t>& sourceIds)
    {
        std::vector<uint32_t> uniqueIds;
        uniqueIds.reserve(sourceIds.size());

        std::unordered_set<uint32_t> seen;
        seen.reserve(sourceIds.size());
        for (uint32_t id : sourceIds)
        {
            if (id == InvalidNodeId || seen.contains(id))
            {
                continue;
            }

            if (scene.GetNodeByInstanceId(id) == nullptr)
            {
                continue;
            }

            seen.insert(id);
            uniqueIds.push_back(id);
        }

        return uniqueIds;
    }

    inline std::vector<uint32_t> BuildRootSelection(Assets::Scene& scene, const std::vector<uint32_t>& sourceIds)
    {
        std::vector<uint32_t> orderedUnique = BuildUniqueValidSelection(scene, sourceIds);

        std::unordered_set<uint32_t> selectedSet;
        selectedSet.reserve(orderedUnique.size());
        for (uint32_t id : orderedUnique)
        {
            selectedSet.insert(id);
        }

        std::vector<uint32_t> roots;
        roots.reserve(orderedUnique.size());
        for (uint32_t id : orderedUnique)
        {
            Assets::Node* node = scene.GetNodeByInstanceId(id);
            if (node == nullptr)
            {
                continue;
            }

            bool parentSelected = false;
            for (Assets::Node* parent = node->GetParent(); parent != nullptr; parent = parent->GetParent())
            {
                if (selectedSet.contains(parent->GetInstanceId()))
                {
                    parentSelected = true;
                    break;
                }
            }

            if (!parentSelected)
            {
                roots.push_back(id);
            }
        }

        return roots;
    }

    inline void RestoreSelection(Assets::Scene& scene, const std::vector<uint32_t>& previousSelection,
                                 uint32_t previousSelectedId)
    {
        if (previousSelection.empty())
        {
            scene.SetSelectedId(previousSelectedId);
            return;
        }

        std::vector<uint32_t> restored = previousSelection;
        auto activeIt = std::find(restored.begin(), restored.end(), previousSelectedId);
        if (activeIt != restored.end() && activeIt + 1 != restored.end())
        {
            uint32_t activeId = *activeIt;
            restored.erase(activeIt);
            restored.push_back(activeId);
        }
        scene.SetSelection(restored);
    }
} // namespace Runtime::Command::SelectionUtils
