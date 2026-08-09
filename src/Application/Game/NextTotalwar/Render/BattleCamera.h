#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Runtime
{
    class TerrainComponent;
}

namespace NextTotalwar
{
    class FBattleCamera
    {
    public:
        void Tick(float deltaSeconds, const Runtime::TerrainComponent* terrain);
        void SetMoveForward(bool value) { moveForward_ = value; }
        void SetMoveBack(bool value) { moveBack_ = value; }
        void SetMoveLeft(bool value) { moveLeft_ = value; }
        void SetMoveRight(bool value) { moveRight_ = value; }
        void SetRotateLeft(bool value) { rotateLeft_ = value; }
        void SetRotateRight(bool value) { rotateRight_ = value; }
        void PanByScreenDelta(const glm::vec2& delta);
        void SetFollowTarget(const glm::vec3& target, bool centerImmediately);
        void ClearFollowTarget();
        void AddZoom(float steps);
        bool OverrideRenderCamera(Assets::Camera& camera) const;
        const glm::vec3& Focus() const { return focus_; }
        float Distance() const { return distance_; }
        float Yaw() const { return yaw_; }
        bool IsFollowing() const { return following_; }

    private:
        glm::vec3 focus_{0.0f, 0.0f, 0.0f};
        float distance_ = 175.0f;
        float yaw_ = 0.0f;
        glm::vec3 followTarget_{0.0f};
        glm::vec3 followOffset_{0.0f};
        bool following_ = false;
        bool moveForward_ = false;
        bool moveBack_ = false;
        bool moveLeft_ = false;
        bool moveRight_ = false;
        bool rotateLeft_ = false;
        bool rotateRight_ = false;
    };
}
