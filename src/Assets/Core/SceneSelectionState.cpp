#include "Assets/Core/SceneSelectionState.hpp"

#include <algorithm>

namespace Assets
{
    bool SceneSelectionState::ContainsId(const std::vector<uint32_t>& ids, uint32_t id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    void SceneSelectionState::AddUniqueId(std::vector<uint32_t>& ids, uint32_t id)
    {
        if (!ContainsId(ids, id))
        {
            ids.push_back(id);
        }
    }

    bool SceneSelectionState::IsSelected(uint32_t id) const
    {
        return ContainsId(selectedIds_, id);
    }

    void SceneSelectionState::SetSingle(uint32_t id)
    {
        selectedIds_.clear();
        selectedId_ = id;
        if (id != invalidNodeId)
        {
            selectedIds_.push_back(id);
        }
    }

    void SceneSelectionState::SetMany(const std::vector<uint32_t>& ids)
    {
        selectedIds_.clear();
        selectedIds_.reserve(ids.size());

        for (uint32_t id : ids)
        {
            AddUniqueId(selectedIds_, id);
        }

        selectedId_ = selectedIds_.empty() ? invalidNodeId : selectedIds_.back();
    }

    void SceneSelectionState::Clear()
    {
        selectedId_ = invalidNodeId;
        selectedIds_.clear();
    }

    void SceneSelectionState::Add(uint32_t id)
    {
        AddUniqueId(selectedIds_, id);
        selectedId_ = id;
    }

    void SceneSelectionState::Remove(uint32_t id)
    {
        if (id == invalidNodeId)
        {
            return;
        }

        selectedIds_.erase(std::remove(selectedIds_.begin(), selectedIds_.end(), id), selectedIds_.end());
        if (selectedIds_.empty())
        {
            Clear();
            return;
        }

        if (selectedId_ == id || !ContainsId(selectedIds_, selectedId_))
        {
            selectedId_ = selectedIds_.back();
        }
    }
} // namespace Assets
