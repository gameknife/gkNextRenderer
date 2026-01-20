#pragma once
#include "Assets/Component.h"
#include "Runtime/NextPhysics.h"
#include <glm/glm.hpp>

namespace Runtime
{
    enum class ENodeMobility
    {
        Static,
        Dynamic,
        Kinematic
    };

    class PhysicsComponent : public Assets::Component
    {
    public:
        PhysicsComponent() = default;

        void BindPhysicsBody(NextBodyID bodyId) { physicsBodyTemp_ = bodyId; }
        const NextBodyID& GetPhysicsBody() const { return physicsBodyTemp_; }

        void SetMobility(ENodeMobility mobility) { mobility_ = mobility; }
        ENodeMobility GetMobility() const { return mobility_; }

        void SetPhysicsOffset(const glm::vec3& offset) { physicsOffset_ = offset; }
        const glm::vec3& GetPhysicsOffset() const { return physicsOffset_; }

    private:
        NextBodyID physicsBodyTemp_;
        ENodeMobility mobility_ = ENodeMobility::Static;
        glm::vec3 physicsOffset_ = glm::vec3(0.0f);
    };
}
