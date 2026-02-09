#include "Assets/Core/Scene.hpp"

#include <algorithm>

namespace Assets
{
    namespace
    {
        constexpr uint32_t InvalidNodeId = SceneSelectionState::invalidNodeId;

        bool IsValidSelectionId(const Scene& scene, uint32_t id)
        {
            return id != InvalidNodeId && scene.GetNodeSharedByInstanceId(id) != nullptr;
        }

        const std::vector<uint32_t>& ResolveEffectiveSelection(const Scene& scene, std::vector<uint32_t>& fallback)
        {
            const auto& ids = scene.GetSelectedIds();
            if (!ids.empty())
            {
                return ids;
            }

            const uint32_t selectedId = scene.GetSelectedId();
            if (selectedId != InvalidNodeId)
            {
                fallback.push_back(selectedId);
            }
            return fallback;
        }
    } // namespace

    void Scene::SetSelectedId(uint32_t id) const
    {
        selectionState_.SetSingle(id);
    }

    void Scene::SetSelection(const std::vector<uint32_t>& ids) const
    {
        std::vector<uint32_t> validIds;
        validIds.reserve(ids.size());

        for (uint32_t id : ids)
        {
            if (!IsValidSelectionId(*this, id))
            {
                continue;
            }

            validIds.push_back(id);
        }

        selectionState_.SetMany(validIds);
    }

    void Scene::ClearSelection() const
    {
        selectionState_.Clear();
    }

    void Scene::AddToSelection(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        selectionState_.Add(id);
    }

    void Scene::RemoveFromSelection(uint32_t id) const
    {
        selectionState_.Remove(id);
    }

    void Scene::ToggleSelection(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        if (selectionState_.IsSelected(id))
        {
            RemoveFromSelection(id);
            return;
        }

        AddToSelection(id);
    }

    bool Scene::IsSelected(uint32_t id) const
    {
        return selectionState_.IsSelected(id);
    }

    void Scene::SetHoveredId(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            hoveredId_ = InvalidNodeId;
            return;
        }
        hoveredId_ = id;
    }

    void Scene::ClearHoveredId() const
    {
        hoveredId_ = InvalidNodeId;
    }

    bool Scene::IsLocked(uint32_t id) const
    {
        return lockedIds_.find(id) != lockedIds_.end();
    }

    void Scene::SetLocked(uint32_t id, bool locked) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        if (locked)
        {
            lockedIds_.insert(id);
            return;
        }

        lockedIds_.erase(id);
    }

    void Scene::ToggleLocked(uint32_t id) const
    {
        if (!IsValidSelectionId(*this, id))
        {
            return;
        }

        if (IsLocked(id))
        {
            lockedIds_.erase(id);
            return;
        }
        lockedIds_.insert(id);
    }

    bool Scene::GetSelectedNodeBounds(glm::vec3& center, float& radius) const
    {
        std::vector<uint32_t> fallbackSelection;
        const auto& ids = ResolveEffectiveSelection(*this, fallbackSelection);

        if (ids.empty())
        {
            return false;
        }

        glm::vec3 minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
        glm::vec3 maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        bool found = false;

        for (uint32_t id : ids)
        {
            glm::vec3 nodeCenter;
            float nodeRadius = 0.0f;
            if (!GetNodeBounds(id, nodeCenter, nodeRadius))
            {
                continue;
            }

            const glm::vec3 extent(nodeRadius);
            minBounds = glm::min(minBounds, nodeCenter - extent);
            maxBounds = glm::max(maxBounds, nodeCenter + extent);
            found = true;
        }

        if (!found)
        {
            return false;
        }

        center = (minBounds + maxBounds) * 0.5f;
        radius = glm::length(maxBounds - minBounds) * 0.5f;
        radius = std::max(radius, 1.0f);
        return true;
    }
} // namespace Assets
