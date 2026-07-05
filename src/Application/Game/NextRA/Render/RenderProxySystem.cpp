#include "Render/RenderProxySystem.h"

#include "Engine/Assets/Core/Node.h"

namespace NextRA
{
    namespace
    {
        glm::vec3 ToRenderVec3(const Sim::WPos& pos)
        {
            constexpr float renderScale = 1.0f / static_cast<float>(Sim::cellSubUnits);
            return glm::vec3(pos.x.ToFloat() * renderScale, pos.y.ToFloat() * renderScale, pos.z.ToFloat() * renderScale);
        }
    }

    void FRenderProxySystem::Clear()
    {
        nodesByRenderId_.clear();
    }

    void FRenderProxySystem::BindNode(uint32_t renderNodeId, std::shared_ptr<Assets::Node> node)
    {
        nodesByRenderId_[renderNodeId] = std::move(node);
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

            std::shared_ptr<Assets::Node> node = it->second.lock();
            if (!node)
            {
                continue;
            }

            const glm::vec3 prev = ToRenderVec3(transform->prevPos);
            const glm::vec3 curr = ToRenderVec3(transform->pos);
            node->SetTranslation(glm::mix(prev, curr, alpha));
        }
    }
}
