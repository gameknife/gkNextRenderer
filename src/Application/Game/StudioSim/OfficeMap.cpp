#include "OfficeMap.h"

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"

namespace StudioSim
{
    namespace
    {
        bool StartsWith(const std::string& s, const char* prefix)
        {
            return s.rfind(prefix, 0) == 0;
        }

        // 把节点名按命名约定归类为 POI；非点位节点返回 false。
        bool ClassifyNode(const std::string& name, std::string& outCategory, ERole& outRole)
        {
            outRole = ERole::Unknown;
            if (StartsWith(name, "desk_"))
            {
                outCategory = "desk";
                // desk_<role>_<id> -> 取中间段作职位标签
                const size_t first = name.find('_');
                const size_t second = name.find('_', first + 1);
                const std::string roleStr = (second == std::string::npos)
                                                ? name.substr(first + 1)
                                                : name.substr(first + 1, second - first - 1);
                outRole = RoleFromString(roleStr);
                return true;
            }
            if (StartsWith(name, "meet_seat_")) { outCategory = "meet";   return true; }
            if (StartsWith(name, "pantry_"))    { outCategory = "pantry"; return true; }
            if (StartsWith(name, "lounge_"))    { outCategory = "lounge"; return true; }
            return false;
        }
    }

    void OfficeMap::BuildFromScene(Assets::Scene& scene)
    {
        points_.clear();

        for (const auto& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }

            std::string category;
            ERole role = ERole::Unknown;
            if (!ClassifyNode(node->GetName(), category, role))
            {
                continue;
            }

            FPointOfInterest poi;
            poi.name = node->GetName();
            poi.category = category;
            poi.roleTag = role;
            poi.worldPos = node->WorldTranslation();
            poi.nodeId = node->GetInstanceId();
            points_.push_back(std::move(poi));
        }

        SPDLOG_INFO("StudioSim/OfficeMap: parsed {} POIs", points_.size());
        for (const auto& p : points_)
        {
            SPDLOG_INFO("  POI {:<18} [{}] role={} world=({:.2f}, {:.2f}, {:.2f})", p.name, p.category,
                        RoleName(p.roleTag), p.worldPos.x, p.worldPos.y, p.worldPos.z);
        }
    }

    std::vector<const FPointOfInterest*> OfficeMap::PointsOfCategory(const std::string& category) const
    {
        std::vector<const FPointOfInterest*> result;
        for (const auto& p : points_)
        {
            if (p.category == category)
            {
                result.push_back(&p);
            }
        }
        return result;
    }

    const FPointOfInterest* OfficeMap::FindByName(const std::string& name) const
    {
        for (const auto& p : points_)
        {
            if (p.name == name)
            {
                return &p;
            }
        }
        return nullptr;
    }

    void OfficeMap::SetWorkable(const std::string& category, const std::string& roleTag, bool workable)
    {
        for (auto& poi : points_)
        {
            if (poi.category == category && (roleTag.empty() || RoleName(poi.roleTag) == roleTag))
            {
                poi.workable = workable;
            }
        }
    }

    void OfficeMap::ResetWorkable()
    {
        for (auto& poi : points_)
        {
            poi.workable = true;
        }
    }
}
