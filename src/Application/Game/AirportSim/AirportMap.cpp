#include "AirportMap.h"

#include "AirportSimConfig.hpp"

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Scene.hpp"

namespace AirportSim
{
    namespace
    {
        const std::vector<std::string> kAnchorCategories = {
            "entrance", "checkin", "kiosk", "security", "gate", "wait",
            "cafe", "food", "shop", "book", "gift", "toilet", "staff",
            "info", "atm", "vending",
        };
    }

    void AirportMap::BuildFromScene(Assets::Scene& scene)
    {
        NextGameplay::Sim::FAnchorParseConfig config;
        config.acceptCategories = kAnchorCategories;
        anchors_.BuildFromScene(scene, config);
        for (FPointOfInterest& point : anchors_.PointsMutable())
        {
            point.worldPos.y = Config::kGroundY;
        }

        SPDLOG_INFO("AirportSim/Map: parsed {} POIs", anchors_.Count());
        for (const auto& p : anchors_.Points())
        {
            SPDLOG_INFO("  POI {:<14} [{}] world=({:.2f}, {:.2f}, {:.2f}) front=({:.2f}, {:.2f})", p.name, p.category,
                        p.worldPos.x, p.worldPos.y, p.worldPos.z, p.frontDir.x, p.frontDir.z);
        }
    }

    std::vector<const FPointOfInterest*> AirportMap::PointsOfCategory(const std::string& category) const
    {
        return anchors_.PointsOfCategory(category);
    }

    const FPointOfInterest* AirportMap::FindByName(const std::string& name) const
    {
        return anchors_.FindByName(name);
    }

    FPointOfInterest* AirportMap::FindByNameMutable(const std::string& name)
    {
        return anchors_.FindByNameMutable(name);
    }

    FPointOfInterest* AirportMap::ClaimFree(const std::string& category, int agentId)
    {
        return anchors_.ClaimFree(category, agentId);
    }

    void AirportMap::Release(const std::string& name, int agentId)
    {
        anchors_.Release(name, agentId);
    }

    int AirportMap::ClaimSeat(const std::string& waitPoiName, int agentId, glm::vec3& outPos)
    {
        return anchors_.ClaimSeat(waitPoiName, agentId, outPos, Config::kSeatSpacing,
                                  Config::kSeatFrontOffset);
    }

    void AirportMap::ReleaseSeat(const std::string& waitPoiName, int slot, int agentId)
    {
        anchors_.ReleaseSeat(waitPoiName, slot, agentId);
    }

    glm::vec3 AirportMap::ServicePoint(const FPointOfInterest& poi, float frontOffset)
    {
        glm::vec3 p = NextGameplay::Sim::FAnchorMap::ServicePoint(poi, frontOffset);
        p.y = Config::kGroundY;
        return p;
    }

    glm::vec3 AirportMap::SeatPosition(const FPointOfInterest& poi, int slot)
    {
        glm::vec3 p = NextGameplay::Sim::FAnchorMap::SeatPosition(
            poi, slot, Config::kSeatSpacing, Config::kSeatFrontOffset);
        p.y = Config::kGroundY;
        return p;
    }
}
