#pragma once

#include "StudioSimTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Assets
{
    class Scene;
}

namespace StudioSim
{
    // 把 SCAD 加载出的场景节点（按命名约定）解析为功能点位锚点表。
    // Prefix rules are defined by BuildFromScene; walls and furniture are ignored.
    class OfficeMap
    {
    public:
        void BuildFromScene(Assets::Scene& scene);
        void Clear();

        const std::vector<FPointOfInterest>& Points() const { return anchors_.Points(); }
        size_t Count() const { return anchors_.Count(); }

        std::vector<const FPointOfInterest*> PointsOfCategory(const std::string& category) const;
        const FPointOfInterest* FindByName(const std::string& name) const;
        ERole RoleForPoint(const std::string& name) const;

        // 把某类点位（roleTag 为空=该类全部，否则按职位标签）标记为可用/不可用（断电/宕机用）。
        void SetWorkable(const std::string& category, const std::string& roleTag, bool workable);
        void ResetWorkable();

    private:
        NextGameplay::Sim::FAnchorMap anchors_;
        std::unordered_map<std::string, ERole> roleTags_;
    };
}
