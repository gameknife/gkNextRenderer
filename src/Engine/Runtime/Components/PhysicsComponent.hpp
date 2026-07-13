#pragma once
#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"
#include <glm/glm.hpp>
#include <string>

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
        REFLECT_COMPONENT(PhysicsComponent)
        
        PhysicsComponent() = default;

        void BindPhysicsBody(NextBodyID bodyId) { physicsBodyTemp_ = bodyId; }
        const NextBodyID& GetPhysicsBody() const { return physicsBodyTemp_; }

        void SetMobility(ENodeMobility mobility) { mobility_ = mobility; }
        ENodeMobility GetMobility() const { return mobility_; }

        void SetPhysicsOffset(const glm::vec3& offset) { physicsOffset_ = offset; }
        glm::vec3 GetPhysicsOffset() const { return physicsOffset_; }

        void SetSimulatePhysics(bool simulatePhysics) { simulatePhysics_ = simulatePhysics; }
        bool GetSimulatePhysics() const { return simulatePhysics_; }

        void SetPhysicsMaterial(const std::string& physicsMaterial) { physicsMaterial_ = physicsMaterial; }
        const std::string& GetPhysicsMaterial() const { return physicsMaterial_; }

        void SetLinearDamping(float linearDamping) { linearDamping_ = linearDamping; }
        float GetLinearDamping() const { return linearDamping_; }

        void SetAngularDamping(float angularDamping) { angularDamping_ = angularDamping; }
        float GetAngularDamping() const { return angularDamping_; }

        void SetEnableGravity(bool enableGravity) { enableGravity_ = enableGravity; }
        bool GetEnableGravity() const { return enableGravity_; }

        void SetCollisionPresets(const std::string& collisionPresets) { collisionPresets_ = collisionPresets; }
        const std::string& GetCollisionPresets() const { return collisionPresets_; }

    private:
        NextBodyID physicsBodyTemp_;
        ENodeMobility mobility_ = ENodeMobility::Static;
        glm::vec3 physicsOffset_ = glm::vec3(0.0f);
        bool simulatePhysics_ = false;
        std::string physicsMaterial_ = "Default";
        float linearDamping_ = 0.0f;
        float angularDamping_ = 0.05f;
        bool enableGravity_ = true;
        std::string collisionPresets_ = "BlockAll";
    };
}
