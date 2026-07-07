#include "Render/RenderProxySystem.h"

#include "Engine/Assets/Core/Node.h"

#include <glm/gtc/quaternion.hpp>

namespace NextRA
{
    namespace
    {
        constexpr float twoPi = 6.28318530717958647692f;

        glm::vec3 ToRenderVec3(const Sim::WPos& pos)
        {
            constexpr float renderScale = 1.0f / static_cast<float>(Sim::cellSubUnits);
            return glm::vec3(pos.x.ToFloat() * renderScale, pos.y.ToFloat() * renderScale, pos.z.ToFloat() * renderScale);
        }

        float InterpolateAngleRaw(Sim::WAngle previous, Sim::WAngle current, float alpha)
        {
            const int32_t delta = Sim::ShortestAngleDiff(current, previous);
            return static_cast<float>(previous.value) + static_cast<float>(delta) * alpha;
        }

        glm::quat YawQuatFromRaw(float rawAngle)
        {
            return glm::angleAxis(rawAngle / static_cast<float>(Sim::angleUnits) * twoPi, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    void FRenderProxySystem::Clear()
    {
        nodesByRenderId_.clear();
    }

    void FRenderProxySystem::BindNode(uint32_t renderNodeId,
                                      std::shared_ptr<Assets::Node> node,
                                      std::shared_ptr<Assets::Node> turretNode)
    {
        nodesByRenderId_[renderNodeId] = FRenderNodes{std::move(node), std::move(turretNode)};
    }

    void FRenderProxySystem::Sync(const Sim::FSimWorld& world, float alpha)
    {
        for (Sim::FActorId actor : world.Actors())
        {
            const Sim::FSimTransform* transform = world.TryGetTransform(actor);
            if (!transform)
            {
                continue;
            }

            const Sim::FRenderLink* renderLink = world.TryGetRenderLink(actor);
            if (!renderLink)
            {
                continue;
            }

            auto it = nodesByRenderId_.find(renderLink->renderNodeId);
            if (it == nodesByRenderId_.end())
            {
                continue;
            }

            std::shared_ptr<Assets::Node> node = it->second.body.lock();
            if (!node)
            {
                continue;
            }

            const glm::vec3 prev = ToRenderVec3(transform->prevPos);
            const glm::vec3 curr = ToRenderVec3(transform->pos);
            const float bodyYaw = InterpolateAngleRaw(transform->prevFacing, transform->facing, alpha);
            node->SetTranslation(glm::mix(prev, curr, alpha));
            node->SetRotation(YawQuatFromRaw(bodyYaw));

            const Sim::FTurret* turret = world.TryGetTurret(actor);
            std::shared_ptr<Assets::Node> turretNode = it->second.turret.lock();
            if (turret && turretNode)
            {
                const float turretYaw = InterpolateAngleRaw(turret->prevFacing, turret->facing, alpha);
                turretNode->SetRotation(YawQuatFromRaw(turretYaw - bodyYaw));
            }
        }
    }
}
