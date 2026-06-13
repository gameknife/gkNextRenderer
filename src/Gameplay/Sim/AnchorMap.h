#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace NextGameplay::Sim
{
    struct FAnchorPoi
    {
        std::string name;
        std::string category;
        glm::vec3 worldPos{0.0f};
        glm::vec3 frontDir{0.0f, 0.0f, 1.0f};
        uint32_t nodeId = 0;
        int occupiedBy = -1;
        int seatOccupied[4] = {-1, -1, -1, -1};
        bool enabled = true;
    };

    struct FAnchorParseConfig
    {
        char categorySeparator = '_';
        bool recoverFrontDir = true;
        std::vector<std::string> acceptCategories;
    };

    class FAnchorMap
    {
    public:
        void BuildFromScene(Assets::Scene& scene, const FAnchorParseConfig& config = {});
        void BuildFromNodes(const std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            const FAnchorParseConfig& config = {});
        void Clear();

        const std::vector<FAnchorPoi>& Points() const { return points_; }
        std::vector<FAnchorPoi>& PointsMutable() { return points_; }
        size_t Count() const { return points_.size(); }

        std::vector<const FAnchorPoi*> PointsOfCategory(const std::string& category) const;
        const FAnchorPoi* FindByName(const std::string& name) const;
        FAnchorPoi* FindByNameMutable(const std::string& name);

        FAnchorPoi* ClaimFree(const std::string& category, int agentId);
        void Release(const std::string& name, int agentId);
        int ClaimSeat(const std::string& poiName, int agentId, glm::vec3& outPos,
                      float spacing = 0.62f, float frontOffset = 0.08f);
        void ReleaseSeat(const std::string& poiName, int slot, int agentId);

        void SetEnabled(const std::string& category, bool enabled);
        void ResetEnabled();

        static glm::vec3 ServicePoint(const FAnchorPoi& poi, float frontOffset);
        static glm::vec3 SeatPosition(const FAnchorPoi& poi, int slot, float spacing, float frontOffset);

    private:
        std::vector<FAnchorPoi> points_;
    };
}
