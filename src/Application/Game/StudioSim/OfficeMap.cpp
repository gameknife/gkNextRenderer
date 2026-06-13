#include "OfficeMap.h"

#include <spdlog/spdlog.h>

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
        NextGameplay::Sim::FAnchorParseConfig config;
        config.recoverFrontDir = false;
        config.acceptCategories = {"desk", "meet_seat", "pantry", "lounge"};
        anchors_.BuildFromScene(scene, config);
        roleTags_.clear();

        for (auto& point : anchors_.PointsMutable())
        {
            if (point.category == "meet_seat")
            {
                point.category = "meet";
            }
            if (point.category == "desk")
            {
                std::string category;
                ERole role = ERole::Unknown;
                ClassifyNode(point.name, category, role);
                roleTags_[point.name] = role;
            }
        }

        SPDLOG_INFO("StudioSim/OfficeMap: parsed {} POIs", anchors_.Count());
        for (const auto& point : anchors_.Points())
        {
            SPDLOG_INFO("  POI {:<18} [{}] role={} world=({:.2f}, {:.2f}, {:.2f})", point.name,
                        point.category, RoleName(RoleForPoint(point.name)), point.worldPos.x,
                        point.worldPos.y, point.worldPos.z);
        }
    }

    void OfficeMap::Clear()
    {
        anchors_.Clear();
        roleTags_.clear();
    }

    std::vector<const FPointOfInterest*> OfficeMap::PointsOfCategory(const std::string& category) const
    {
        return anchors_.PointsOfCategory(category);
    }

    const FPointOfInterest* OfficeMap::FindByName(const std::string& name) const
    {
        return anchors_.FindByName(name);
    }

    ERole OfficeMap::RoleForPoint(const std::string& name) const
    {
        const auto found = roleTags_.find(name);
        return found == roleTags_.end() ? ERole::Unknown : found->second;
    }

    void OfficeMap::SetWorkable(const std::string& category, const std::string& roleTag, bool workable)
    {
        for (auto& point : anchors_.PointsMutable())
        {
            if (point.category == category &&
                (roleTag.empty() || RoleName(RoleForPoint(point.name)) == roleTag))
            {
                point.enabled = workable;
            }
        }
    }

    void OfficeMap::ResetWorkable()
    {
        anchors_.ResetEnabled();
    }
}
