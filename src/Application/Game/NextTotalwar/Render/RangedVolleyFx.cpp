#include "Render/RangedVolleyFx.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace NextTotalwar
{
    void FRangedVolleyFx::Initialize(std::vector<std::shared_ptr<Assets::Node>> arrowPool)
    {
        arrows_.clear();
        arrows_.reserve(arrowPool.size());
        for (std::shared_ptr<Assets::Node>& node : arrowPool)
        {
            Assets::NodeUtils::SetVisible(node, false);
            arrows_.push_back({std::move(node)});
        }
        nextArrow_ = 0;
        volleySerial_ = 0;
    }

    void FRangedVolleyFx::ResetBattle()
    {
        for (FArrow& arrow : arrows_)
        {
            arrow.active = false;
            arrow.age = 0.0f;
            if (arrow.node) Assets::NodeUtils::SetVisible(arrow.node, false);
        }
        nextArrow_ = 0;
        volleySerial_ = 0;
    }

    void FRangedVolleyFx::SpawnVolley(const FCombatEvent& event,
                                      const std::vector<FRegiment>& regiments)
    {
        if (arrows_.empty() || event.sourceRegiment < 0 ||
            static_cast<size_t>(event.sourceRegiment) >= regiments.size())
            return;
        const glm::vec3 source = regiments[static_cast<size_t>(event.sourceRegiment)].anchor;
        constexpr int representativeArrows = 6;
        for (int index = 0; index < representativeArrows; ++index)
        {
            FArrow& arrow = arrows_[nextArrow_];
            nextArrow_ = (nextArrow_ + 1) % arrows_.size();
            const uint32_t hash = volleySerial_ * 1664525U + static_cast<uint32_t>(index) * 1013904223U;
            const float lateral = (static_cast<float>(hash & 255U) / 255.0f - 0.5f) * 10.0f;
            const float depth = (static_cast<float>((hash >> 8U) & 255U) / 255.0f - 0.5f) * 7.0f;
            arrow.start = source + glm::vec3(0.0f, 1.8f, lateral * 0.12f);
            arrow.end = event.worldPos + glm::vec3(depth, 0.5f, lateral);
            const float distance = glm::length(glm::vec2(arrow.end.x - arrow.start.x,
                                                          arrow.end.z - arrow.start.z));
            arrow.duration = glm::clamp(distance / 48.0f, 0.45f, 1.5f);
            arrow.arcHeight = glm::clamp(distance * 0.18f, 4.0f, 14.0f);
            arrow.age = 0.0f;
            arrow.active = true;
            if (arrow.node) Assets::NodeUtils::SetVisible(arrow.node, true);
        }
        ++volleySerial_;
    }

    void FRangedVolleyFx::Tick(float deltaSeconds, const std::vector<FRegiment>& regiments,
                               const std::vector<FCombatEvent>& events)
    {
        for (const FCombatEvent& event : events)
        {
            if (event.type == ECombatEventType::Volley) SpawnVolley(event, regiments);
        }
        for (FArrow& arrow : arrows_)
        {
            if (!arrow.active || !arrow.node) continue;
            arrow.age += std::max(deltaSeconds, 0.0f);
            const float t = glm::clamp(arrow.age / std::max(arrow.duration, 0.01f), 0.0f, 1.0f);
            glm::vec3 position = glm::mix(arrow.start, arrow.end, t);
            position.y += 4.0f * arrow.arcHeight * t * (1.0f - t);
            const float nextT = glm::min(t + 0.01f, 1.0f);
            glm::vec3 next = glm::mix(arrow.start, arrow.end, nextT);
            next.y += 4.0f * arrow.arcHeight * nextT * (1.0f - nextT);
            const glm::vec3 directionDelta = next - position;
            arrow.node->Translation() = position;
            const float directionLength = glm::length(directionDelta);
            if (directionLength > 0.01f)
            {
                const glm::vec3 direction = directionDelta / directionLength;
                const float yaw = std::atan2(direction.x, direction.z);
                const float pitch = -std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
                arrow.node->Rotation() =
                    glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            arrow.node->RecalcTransform(true);
            if (t >= 1.0f)
            {
                arrow.active = false;
                Assets::NodeUtils::SetVisible(arrow.node, false);
            }
        }
    }

    int FRangedVolleyFx::ActiveCount() const
    {
        return static_cast<int>(std::count_if(arrows_.begin(), arrows_.end(), [](const FArrow& arrow)
        {
            return arrow.active;
        }));
    }
}
