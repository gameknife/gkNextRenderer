#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace NextGameplay::Sim
{
    enum class EAnimHint
    {
        Idle,
        Walk,
        Sit,
        Work
    };

    class ISimVisual
    {
    public:
        virtual ~ISimVisual() = default;
        virtual void SetWorldTransform(const glm::vec3& position, float yaw) = 0;
        virtual void SetAnimHint(EAnimHint hint) = 0;
        virtual void SetVisible(bool visible) = 0;
        virtual void SetMoveSpeed(float metersPerSecond) {}
        virtual void Tick(float deltaSeconds) {}
    };

    class FGeometryVisual final : public ISimVisual
    {
    public:
        FGeometryVisual(std::shared_ptr<Assets::Node> node, glm::vec3 parkedPosition);

        void SetWorldTransform(const glm::vec3& position, float yaw) override;
        void SetAnimHint(EAnimHint hint) override;
        void SetVisible(bool visible) override;

        EAnimHint CurrentHint() const { return hint_; }

    private:
        std::shared_ptr<Assets::Node> node_;
        glm::vec3 parkedPosition_{0.0f, -100.0f, 0.0f};
        EAnimHint hint_ = EAnimHint::Idle;
    };
}
