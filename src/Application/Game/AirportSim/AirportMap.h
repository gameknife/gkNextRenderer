#pragma once

#include "AirportSimTypes.h"

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Assets
{
    class Scene;
}

namespace AirportSim
{
    // 把 airport.scad 加载出的具名锚点节点解析为 POI 表（仿 StudioSim::OfficeMap，§2.3）。
    // frontDir 从节点世界旋转恢复：scad 局部 -y → 引擎局部 +z。
    class AirportMap
    {
    public:
        void BuildFromScene(Assets::Scene& scene);
        void Clear() { points_.clear(); }

        const std::vector<FPointOfInterest>& Points() const { return points_; }
        std::vector<FPointOfInterest>& PointsMutable() { return points_; }
        size_t Count() const { return points_.size(); }

        std::vector<const FPointOfInterest*> PointsOfCategory(const std::string& category) const;
        const FPointOfInterest* FindByName(const std::string& name) const;
        FPointOfInterest* FindByNameMutable(const std::string& name);

        // 占用一个该类别下空闲的 POI（occupiedBy<0），返回 nullptr = 全忙。
        FPointOfInterest* ClaimFree(const std::string& category, int agentId);
        void Release(const std::string& name, int agentId);

        // wait 类 4 联座：占一个空 seat slot，返回 slot 下标（-1 = 满）；outPos 为坐席世界坐标。
        int ClaimSeat(const std::string& waitPoiName, int agentId, glm::vec3& outPos);
        void ReleaseSeat(const std::string& waitPoiName, int slot, int agentId);

        // 常用派生点位。
        static glm::vec3 ServicePoint(const FPointOfInterest& poi, float frontOffset);
        static glm::vec3 SeatPosition(const FPointOfInterest& poi, int slot);

    private:
        std::vector<FPointOfInterest> points_;
    };
}
