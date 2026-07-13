#include "AnchorMap.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace NextGameplay::Sim
{
    namespace
    {
        bool ParseCategory(const std::string& name, const FAnchorParseConfig& config, std::string& outCategory)
        {
            if (!config.acceptCategories.empty())
            {
                const std::string* bestMatch = nullptr;
                const std::string separator(1, config.categorySeparator);
                for (const std::string& category : config.acceptCategories)
                {
                    if ((name == category || name.rfind(category + separator, 0) == 0) &&
                        (bestMatch == nullptr || category.size() > bestMatch->size()))
                    {
                        bestMatch = &category;
                    }
                }
                if (bestMatch == nullptr)
                {
                    return false;
                }
                outCategory = *bestMatch;
                return true;
            }

            const size_t separator = name.find(config.categorySeparator);
            if (separator == std::string::npos || separator == 0)
            {
                return false;
            }
            outCategory = name.substr(0, separator);
            return true;
        }
    }

    void FAnchorMap::BuildFromScene(Assets::Scene& scene, const FAnchorParseConfig& config)
    {
        BuildFromNodes(scene.Nodes(), config);
    }

    void FAnchorMap::BuildFromNodes(const std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                    const FAnchorParseConfig& config)
    {
        points_.clear();
        for (const auto& node : nodes)
        {
            if (!node)
            {
                continue;
            }

            std::string category;
            if (!ParseCategory(node->GetName(), config, category))
            {
                continue;
            }

            FAnchorPoi poi;
            poi.name = node->GetName();
            poi.category = std::move(category);
            poi.worldPos = node->WorldTranslation();
            poi.nodeId = node->GetInstanceId();
            if (config.recoverFrontDir)
            {
                glm::vec3 front = node->WorldRotation() * glm::vec3(0.0f, 0.0f, 1.0f);
                front.y = 0.0f;
                const float length = glm::length(front);
                if (length > 0.001f)
                {
                    poi.frontDir = front / length;
                }
            }
            points_.push_back(std::move(poi));
        }
    }

    void FAnchorMap::Clear()
    {
        points_.clear();
    }

    std::vector<const FAnchorPoi*> FAnchorMap::PointsOfCategory(const std::string& category) const
    {
        std::vector<const FAnchorPoi*> result;
        for (const FAnchorPoi& point : points_)
        {
            if (point.category == category)
            {
                result.push_back(&point);
            }
        }
        return result;
    }

    const FAnchorPoi* FAnchorMap::FindByName(const std::string& name) const
    {
        const auto it = std::find_if(points_.begin(), points_.end(),
                                     [&name](const FAnchorPoi& point) { return point.name == name; });
        return it == points_.end() ? nullptr : &*it;
    }

    FAnchorPoi* FAnchorMap::FindByNameMutable(const std::string& name)
    {
        const auto it = std::find_if(points_.begin(), points_.end(),
                                     [&name](const FAnchorPoi& point) { return point.name == name; });
        return it == points_.end() ? nullptr : &*it;
    }

    FAnchorPoi* FAnchorMap::ClaimFree(const std::string& category, int agentId)
    {
        for (FAnchorPoi& point : points_)
        {
            if (point.category == category && point.enabled && point.occupiedBy < 0)
            {
                point.occupiedBy = agentId;
                return &point;
            }
        }
        return nullptr;
    }

    void FAnchorMap::Release(const std::string& name, int agentId)
    {
        FAnchorPoi* point = FindByNameMutable(name);
        if (point != nullptr && point->occupiedBy == agentId)
        {
            point->occupiedBy = -1;
        }
    }

    int FAnchorMap::ClaimSeat(const std::string& poiName, int agentId, glm::vec3& outPos,
                              float spacing, float frontOffset)
    {
        FAnchorPoi* point = FindByNameMutable(poiName);
        if (point == nullptr || !point->enabled)
        {
            return -1;
        }
        for (int slot = 0; slot < 4; ++slot)
        {
            if (point->seatOccupied[slot] < 0)
            {
                point->seatOccupied[slot] = agentId;
                outPos = SeatPosition(*point, slot, spacing, frontOffset);
                return slot;
            }
        }
        return -1;
    }

    void FAnchorMap::ReleaseSeat(const std::string& poiName, int slot, int agentId)
    {
        FAnchorPoi* point = FindByNameMutable(poiName);
        if (point != nullptr && slot >= 0 && slot < 4 && point->seatOccupied[slot] == agentId)
        {
            point->seatOccupied[slot] = -1;
        }
    }

    void FAnchorMap::SetEnabled(const std::string& category, bool enabled)
    {
        for (FAnchorPoi& point : points_)
        {
            if (point.category == category)
            {
                point.enabled = enabled;
            }
        }
    }

    void FAnchorMap::ResetEnabled()
    {
        for (FAnchorPoi& point : points_)
        {
            point.enabled = true;
        }
    }

    glm::vec3 FAnchorMap::ServicePoint(const FAnchorPoi& poi, float frontOffset)
    {
        return poi.worldPos + poi.frontDir * frontOffset;
    }

    glm::vec3 FAnchorMap::SeatPosition(const FAnchorPoi& poi, int slot, float spacing, float frontOffset)
    {
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), poi.frontDir);
        const float rightLength = glm::length(right);
        right = rightLength > 0.001f ? right / rightLength : glm::vec3(1.0f, 0.0f, 0.0f);
        const float lateral = (static_cast<float>(slot) - 1.5f) * spacing;
        return poi.worldPos + right * lateral + poi.frontDir * frontOffset;
    }
}
