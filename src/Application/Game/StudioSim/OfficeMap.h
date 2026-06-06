#pragma once

#include "StudioSimTypes.h"

#include <string>
#include <vector>

namespace Assets
{
    class Scene;
}

namespace StudioSim
{
    // 把 SCAD 加载出的场景节点（按命名约定）解析为功能点位锚点表。
    // 见 docs/StudioSim-MVP-Plan.md §6 / §10。仅认点位前缀，墙体/家具节点被忽略。
    class OfficeMap
    {
    public:
        void BuildFromScene(Assets::Scene& scene);
        void Clear() { points_.clear(); }

        const std::vector<FPointOfInterest>& Points() const { return points_; }
        size_t Count() const { return points_.size(); }

        std::vector<const FPointOfInterest*> PointsOfCategory(const std::string& category) const;
        const FPointOfInterest* FindByName(const std::string& name) const;

        // 把某类点位（roleTag 为空=该类全部，否则按职位标签）标记为可用/不可用（断电/宕机用）。
        void SetWorkable(const std::string& category, const std::string& roleTag, bool workable);
        void ResetWorkable();

    private:
        std::vector<FPointOfInterest> points_;
    };
}
