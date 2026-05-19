#pragma once

#include <cstdint>
#include <vector>

namespace Assets
{
    class SceneSelectionState final
    {
    public:
        static constexpr uint32_t invalidNodeId = static_cast<uint32_t>(-1);

        uint32_t GetPrimaryId() const { return selectedId_; }
        const std::vector<uint32_t>& GetIds() const { return selectedIds_; }

        bool IsSelected(uint32_t id) const;
        void SetSingle(uint32_t id);
        void SetMany(const std::vector<uint32_t>& ids);
        void Clear();
        void Add(uint32_t id);
        void Remove(uint32_t id);

    private:
        static bool ContainsId(const std::vector<uint32_t>& ids, uint32_t id);
        static void AddUniqueId(std::vector<uint32_t>& ids, uint32_t id);

        uint32_t selectedId_ = invalidNodeId;
        std::vector<uint32_t> selectedIds_;
    };
} // namespace Assets
