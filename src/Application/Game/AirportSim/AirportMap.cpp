#include "AirportMap.h"

#include "AirportSimConfig.hpp"

#include <cstring>

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"

namespace AirportSim
{
    namespace
    {
        // §2.2 锚点类别前缀表。命中返回类别名（= 前缀去掉下划线）。
        const char* kAnchorPrefixes[] = {
            "entrance_", "checkin_", "kiosk_", "security_", "gate_", "wait_",
            "cafe_", "food_", "shop_", "book_", "gift_", "toilet_", "staff_",
            "info_", "atm_", "vending_",
        };

        bool ClassifyNode(const std::string& name, std::string& outCategory)
        {
            for (const char* prefix : kAnchorPrefixes)
            {
                if (name.rfind(prefix, 0) == 0)
                {
                    outCategory.assign(prefix, std::strlen(prefix) - 1); // 去掉尾部下划线
                    return true;
                }
            }
            return false;
        }
    }

    void AirportMap::BuildFromScene(Assets::Scene& scene)
    {
        points_.clear();

        for (const auto& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }

            std::string category;
            if (!ClassifyNode(node->GetName(), category))
            {
                continue;
            }

            FPointOfInterest poi;
            poi.name = node->GetName();
            poi.category = category;
            poi.worldPos = node->WorldTranslation();
            poi.worldPos.y = Config::kGroundY;
            // scad front = 局部 -y；Z-up→Y-up（world=(x,z,−y)）后即引擎局部 +z。
            glm::vec3 front = node->WorldRotation() * glm::vec3(0.0f, 0.0f, 1.0f);
            front.y = 0.0f;
            const float len = glm::length(front);
            poi.frontDir = len > 0.001f ? front / len : glm::vec3(0.0f, 0.0f, 1.0f);
            poi.nodeId = node->GetInstanceId();
            points_.push_back(std::move(poi));
        }

        SPDLOG_INFO("AirportSim/Map: parsed {} POIs", points_.size());
        for (const auto& p : points_)
        {
            SPDLOG_INFO("  POI {:<14} [{}] world=({:.2f}, {:.2f}, {:.2f}) front=({:.2f}, {:.2f})", p.name, p.category,
                        p.worldPos.x, p.worldPos.y, p.worldPos.z, p.frontDir.x, p.frontDir.z);
        }
    }

    std::vector<const FPointOfInterest*> AirportMap::PointsOfCategory(const std::string& category) const
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

    const FPointOfInterest* AirportMap::FindByName(const std::string& name) const
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

    FPointOfInterest* AirportMap::FindByNameMutable(const std::string& name)
    {
        for (auto& p : points_)
        {
            if (p.name == name)
            {
                return &p;
            }
        }
        return nullptr;
    }

    FPointOfInterest* AirportMap::ClaimFree(const std::string& category, int agentId)
    {
        for (auto& p : points_)
        {
            if (p.category == category && p.occupiedBy < 0)
            {
                p.occupiedBy = agentId;
                return &p;
            }
        }
        return nullptr;
    }

    void AirportMap::Release(const std::string& name, int agentId)
    {
        FPointOfInterest* poi = FindByNameMutable(name);
        if (poi != nullptr && poi->occupiedBy == agentId)
        {
            poi->occupiedBy = -1;
        }
    }

    int AirportMap::ClaimSeat(const std::string& waitPoiName, int agentId, glm::vec3& outPos)
    {
        FPointOfInterest* poi = FindByNameMutable(waitPoiName);
        if (poi == nullptr)
        {
            return -1;
        }
        for (int i = 0; i < 4; ++i)
        {
            if (poi->seatOccupied[i] < 0)
            {
                poi->seatOccupied[i] = agentId;
                outPos = SeatPosition(*poi, i);
                return i;
            }
        }
        return -1;
    }

    void AirportMap::ReleaseSeat(const std::string& waitPoiName, int slot, int agentId)
    {
        FPointOfInterest* poi = FindByNameMutable(waitPoiName);
        if (poi != nullptr && slot >= 0 && slot < 4 && poi->seatOccupied[slot] == agentId)
        {
            poi->seatOccupied[slot] = -1;
        }
    }

    glm::vec3 AirportMap::ServicePoint(const FPointOfInterest& poi, float frontOffset)
    {
        glm::vec3 p = poi.worldPos + poi.frontDir * frontOffset;
        p.y = Config::kGroundY;
        return p;
    }

    glm::vec3 AirportMap::SeatPosition(const FPointOfInterest& poi, int slot)
    {
        const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), poi.frontDir));
        const float lateral = (static_cast<float>(slot) - 1.5f) * Config::kSeatSpacing;
        glm::vec3 p = poi.worldPos + right * lateral + poi.frontDir * Config::kSeatFrontOffset;
        p.y = Config::kGroundY;
        return p;
    }
}
